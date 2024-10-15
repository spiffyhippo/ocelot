#ifndef SRC_WORKER_H_
#define SRC_WORKER_H_

// Copyright [2017-2024] Orpheus

#include <spdlog/spdlog.h>
#include <boost/beast/http.hpp>  // Include Boost.Beast for HTTP parsing
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <iostream>
#include <memory>
#include <shared_mutex>  // For reader-writer lock (shared_mutex)
#include <ctime>
#include <random>
#include "site_comm.h"

enum tracker_status { OPEN, PAUSED, CLOSING };  // tracker status

class worker {
 private:
    config * conf;
    mysql * db;
    site_comm * s_comm;
    torrent_list &torrents_list;
    user_list &users_list;
    std::vector<std::string> &whitelist;
    std::unordered_map<std::string, del_message> del_reasons;
    tracker_status status;
    int site_freeleech;
    bool reaper_active;
    time_t cur_time;
    std::shared_ptr<spdlog::logger> logger;
    std::mt19937 randgen;
    std::uniform_int_distribution<int> jitter;

    // Mutexes for thread safety
    static std::shared_mutex torrent_list_mutex;  // Changed to shared_mutex for read-heavy operations
    static std::shared_mutex user_list_mutex;     // Changed to shared_mutex for user-related operations
    static std::mutex client_len_mutex;           // Retained for client length tracking
    std::mutex del_reasons_lock;                  // Retained for managing deletion reasons

    unsigned int announce_interval;
    unsigned int del_reason_lifetime;
    unsigned int peers_timeout;
    unsigned int numwant_limit;
    bool keepalive_enabled;
    std::string site_password;
    std::string report_password;

    void load_config(config * conf);
    void do_start_reaper();
    void reap_peers();
    void reap_del_reasons();
    std::string get_del_reason(int code);
    peer_list::iterator add_peer(peer_list &peer_list, const std::string &peer_id);
    inline bool peer_is_visible(user_ptr &u, peer *p);

    // New utility functions for parsing and HMAC validation
    std::string extract_passkey(const std::string_view &input);  // Added to extract passkey using string_view
    std::string generate_hmac(const std::string &input, const std::string &key);  // HMAC generation function

 public:
    worker(config * conf_obj, int freeleech, torrent_list &torrents, user_list &users, std::vector<std::string> &_whitelist, mysql * db_obj, site_comm * sc);
    
    void reload_config(config * conf);
    std::string work(const std::string &input, std::string &ip, client_opts_t &client_opts);

    // Updated announce method to take string_view for improved performance
    std::string announce(const std::string_view &input, user_ptr &u, params_type &params, params_type &headers, std::string &ip, client_opts_t &client_opts);

    std::string scrape(const std::list<std::string> &infohashes, params_type &headers, client_opts_t &client_opts);
    std::string update(params_type &params, client_opts_t &client_opts);

    void reload_lists();
    bool shutdown();

    tracker_status get_status() { return status; }

    void start_reaper();
};

#endif  // SRC_WORKER_H_
