// Copyright [2017-2024] Orpheus

#include <spdlog/spdlog.h>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <list>
#include <vector>
#include <set>
#include <algorithm>
#include <mutex>
#include <shared_mutex>  // Reader-writer locks
#include <thread>
#include <utility>

#include "ocelot.h"
#include "config.h"
#include "db.h"
#include "worker.h"
#include "misc_functions.h"
#include "site_comm.h"
#include "response.h"
#include "report.h"
#include "user.h"

// Use shared mutex for read-heavy operations
std::shared_mutex worker::torrent_list_mutex;
std::shared_mutex worker::user_list_mutex;

//---------- Worker - does stuff with input
worker::worker(config * conf_obj, int freeleech, torrent_list &torrents, user_list &users, std::vector<std::string> &_whitelist, mysql * db_obj, site_comm * sc) :
    conf(conf_obj), db(db_obj), s_comm(sc), site_freeleech(freeleech), torrents_list(torrents), users_list(users), whitelist(_whitelist), status(OPEN), reaper_active(false), randgen((std::random_device())()) {
    logger = spdlog::get("logger");
    load_config(conf);
}

void worker::load_config(config * conf) {
    announce_interval   = conf->get_uint("announce_interval");
    del_reason_lifetime = conf->get_uint("del_reason_lifetime");
    peers_timeout       = conf->get_uint("peers_timeout");
    numwant_limit       = conf->get_uint("numwant_limit");
    keepalive_enabled   = conf->get_uint("keepalive_timeout") != 0;
    site_password       = conf->get_str("site_password");
    report_password     = conf->get_str("report_password");
    jitter              = std::uniform_int_distribution<int>(0, conf->get_uint("announce_jitter"));
}

void worker::reload_config(config * conf) {
    load_config(conf);
}

void worker::reload_lists() {
    std::unique_lock<std::shared_mutex> lock_torrents(torrent_list_mutex);
    std::unique_lock<std::shared_mutex> lock_users(user_list_mutex);

    status = PAUSED;
    db->load_freeleech(site_freeleech);
    db->load_torrents(torrents_list);
    db->load_users(users_list);
    db->load_whitelist(whitelist);
    status = OPEN;
}

bool worker::shutdown() {
    if (status == OPEN) {
        status = CLOSING;
        logger->info("closing tracker... press Ctrl-C again to terminate");
        return false;
    } else if (status == CLOSING) {
        logger->info("shutting down uncleanly");
        return true;
    } else {
        return false;
    }
}

std::string worker::work(const std::string &input, std::string &ip, client_opts_t &client_opts) {
    // Use Boost.Beast to parse HTTP request more efficiently
    boost::beast::http::request<boost::beast::http::string_body> req;
    boost::beast::http::parser<boost::beast::http::request<boost::beast::http::string_body>> parser{req};
    parser.eager(true);
    parser.put(boost::asio::buffer(input));

    // Validate input and request length
    if (input.length() < 60) {
        stats.http_error++;
        return error("GET string too short", client_opts);
    }

    // Extract passkey and other critical parameters
    std::string passkey = extract_passkey(req.target());  // Assuming this utility function exists
    if (passkey.empty()) {
        return error("Malformed announce", client_opts);
    }

    // Extract action and handle accordingly
    auto action = parse_action(req.target());
    if (action == INVALID) {
        stats.http_error++;
        return error("Invalid action", client_opts);
    }

    // Authenticate using HMAC
    std::string expected_hmac = generate_hmac(input, conf->get_str("hmac_key"));
    if (params["hmac"] != expected_hmac) {
        stats.auth_error_secret++;
        return error("Authentication failure", client_opts);
    }

    // Parse headers, parameters and pass to appropriate functions
    // Example for announce action
    if (action == ANNOUNCE) {
        user_ptr u;
        {
            std::shared_lock<std::shared_mutex> ul_lock(user_list_mutex);
            auto user_it = users_list.find(passkey);
            if (user_it == users_list.end()) {
                stats.auth_error_announce_key++;
                return error("Passkey not found", client_opts);
            }
            u = user_it->second;
        }
        return announce(req.target(), u, params, headers, ip, client_opts);
    }

    return http_response("success", client_opts);
}

std::string worker::announce(const std::string_view &input, user_ptr &u, params_type &params, params_type &headers, std::string &ip, client_opts_t &client_opts) {
    cur_time = time(NULL);

    // Extract peer_id using string_view to avoid copying
    auto peer_id_view = extract_peer_id(input);

    // Check torrent existence under shared lock
    std::shared_lock<std::shared_mutex> lock_torrents(torrent_list_mutex);
    auto torrent_it = torrents_list.find(hex_decode(params["info_hash"]));
    if (torrent_it == torrents_list.end()) {
        return error("Unregistered torrent", client_opts);
    }
    auto &tor = torrent_it->second;

    // Other operations like peer updating, compact response generation, etc.

    return http_response("d8:completei" + std::to_string(tor.seeders.size()) + "e", client_opts);
}

std::string worker::scrape(const std::list<std::string> &infohashes, params_type &headers, client_opts_t &client_opts) {
    std::string output = "d5:filesd";
    std::shared_lock<std::shared_mutex> lock_torrents(torrent_list_mutex);
    for (const auto& infohash : infohashes) {
        std::string decoded_infohash = hex_decode(infohash);
        auto tor = torrents_list.find(decoded_infohash);
        if (tor != torrents_list.end()) {
            const auto& torrent = tor->second;
            output += inttostr(decoded_infohash.length()) + ':' + decoded_infohash;
            output += "d8:completei" + inttostr(torrent.seeders.size()) + "e10:incompletei" + inttostr(torrent.leechers.size()) + "e";
        }
    }
    output += "ee";
    return http_response(output, client_opts);
}

void worker::start_reaper() {
    if (!reaper_active) {
        std::thread thread(&worker::do_start_reaper, this);
        thread.detach();
    }
}

void worker::do_start_reaper() {
    reaper_active = true;
    reap_peers();
    reap_del_reasons();
    reaper_active = false;
}

void worker::reap_peers() {
    logger->info("Starting peer reaper");
    cur_time = time(NULL);

    std::unique_lock<std::shared_mutex> lock_torrents(torrent_list_mutex);
    unsigned int reaped_l = 0, reaped_s = 0;

    for (auto &torrent_pair : torrents_list) {
        auto &torrent = torrent_pair.second;
        bool reaped = false;

        auto peer_it = torrent.leechers.begin();
        while (peer_it != torrent.leechers.end()) {
            if (peer_it->second.last_announced + peers_timeout < cur_time) {
                peer_it = torrent.leechers.erase(peer_it);
                reaped = true;
                reaped_l++;
            } else {
                ++peer_it;
            }
        }

        peer_it = torrent.seeders.begin();
        while (peer_it != torrent.seeders.end()) {
            if (peer_it->second.last_announced + peers_timeout < cur_time) {
                peer_it = torrent.seeders.erase(peer_it);
                reaped = true;
                reaped_s++;
            } else {
                ++peer_it;
            }
        }
    }
    logger->info("Reaped {} leechers and {} seeders", reaped_l, reaped_s);
}
