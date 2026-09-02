#include "json_config.hpp"

#include <vector>
#include <fstream>
#include <filesystem>
#include <fmt/ranges.h>

namespace Omni::Config {

bool is_json_launch(int argc, char* argv[]) {
    return argc > 1 && argv[1][0] != '-';
}


std::vector<std::string> json_launch_paths(int argc, char* argv[]) {
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            throw std::runtime_error(fmt::format(
                "cannot mix flags with launch-config files (got \"{}\")", argv[i]
            ));
        }
        paths.emplace_back(argv[i]);
    }
    return paths;
}


nlohmann::json load_json_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error(fmt::format("config file not found: {}", file_path));
    }

    try {
        return nlohmann::json::parse(file);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            fmt::format("failed to parse {}: {}", file_path, e.what())
        );
    }
}


std::string named_log_path(const std::string& log_path, const std::string& name) {
    if (name.empty()) return log_path;
    std::filesystem::path path(log_path);
    // The name goes between the directory and the file, not on either: the file keeps
    // the service's own name so `trader.log` still reads as one.
    return (path.parent_path() / name / path.filename()).string();
}


std::string named_save_path(const std::string& save_path, const std::string& name) {
    if (name.empty()) return save_path;
    return (std::filesystem::path(save_path) / name).string();
}


JsonSection::JsonSection(const nlohmann::json& doc, std::string name)
    : obj_(nullptr), name_(std::move(name))
{
    auto it = doc.find(name_);
    if (it == doc.end()) {
        throw std::runtime_error(fmt::format("missing section \"{}\"", name_));
    }
    if (!it->is_object()) {
        throw std::runtime_error(fmt::format("section \"{}\" is not an object", name_));
    }
    obj_ = &(*it);
}


bool JsonSection::has(const char* key) const {
    return obj_->contains(key) && !obj_->at(key).is_null();
}


JsonSection& JsonSection::skip(const char* key) {
    known_.insert(key);
    return *this;
}


void JsonSection::done() const {
    std::vector<std::string> unknown;
    for (const auto& [key, value] : obj_->items()) {
        if (!known_.contains(key)) {
            unknown.push_back(key);
        }
    }
    if (unknown.empty()) {
        return;
    }
    throw std::runtime_error(fmt::format(
        "section \"{}\" has unknown field(s): {}", name_, fmt::join(unknown, ", ")
    ));
}

} // namespace Omni::Config
