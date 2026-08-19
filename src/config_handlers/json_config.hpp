#pragma once

#include <set>
#include <string>
#include <vector>
#include <stdexcept>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

// Launch-config support: the file-based alternative to the CLI. Every config struct
// keeps its argparse set_parser()/init() pair and gains a parse_json() that reads the
// same fields out of one section of a JSON document, so a process can be started
// either way:
//
//     build/listener --exchange binance --products BTCUSDT:futures
//     build/listener config/binance/listener0.json
//
// The files live under config/<exchange>/, one per config struct. Credentials stay
// under account/<exchange>/ and are never part of a launch config.

namespace Omni::Config {

// True when argv describes a file-based launch rather than a flag-based one: there is
// at least one argument after argv[0] and the first of them is not an option.
bool is_json_launch(int argc, char* argv[]);

// The launch-config paths, in the order given (argv[1..]). The two launch forms are
// not mixable -- a launch config states every field it cares about, so a flag mixed
// in among the paths is a mistake rather than an override, and throws.
std::vector<std::string> json_launch_paths(int argc, char* argv[]);

// Read and parse a launch-config file. Throws with the file path in the message --
// these are hand-written files started from a shell, so a bad one should say what is
// wrong and stop, not fall back to defaults.
nlohmann::json load_json_file(const std::string& file_path);


// One section of a launch-config document (e.g. the "listener_config" object).
//
// Individual fields are optional: a key that is absent leaves the struct's own
// default in place, matching what the CLI does. A key that is *present but
// unrecognized* is an error. That asymmetry is deliberate -- a misspelled
// "order_lts" would otherwise be silently ignored and the trader would run at the
// default of zero, which looks like a working process placing no orders rather than
// like a broken config.
class JsonSection {
public:
    // `doc` is the whole parsed file; the named section is looked up in it and is
    // required to be present and an object.
    JsonSection(const nlohmann::json& doc, std::string name);

    bool has(const char* key) const;

    // Assign section[key] into `out`, leaving `out` untouched if the key is absent.
    template<typename T>
    JsonSection& get(const char* key, T& out) {
        known_.insert(key);
        auto it = obj_->find(key);
        if (it == obj_->end() || it->is_null()) {
            return *this;
        }
        try {
            it->get_to(out);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                fmt::format("{}.{}: {}", name_, key, e.what())
            );
        }
        return *this;
    }

    // Declare a key the caller reads by hand (e.g. a product token list that needs
    // parsing rather than a straight assignment) so done() does not flag it.
    JsonSection& skip(const char* key);

    // Throws listing every key in the section that no get()/skip() claimed.
    void done() const;

private:
    const nlohmann::json* obj_;
    std::string name_;
    std::set<std::string> known_;
};

} // namespace Omni::Config
