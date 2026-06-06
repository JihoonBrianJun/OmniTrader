#pragma once

#include <string>
#include <memory>
#include <quill/Frontend.h>
#include <quill/sinks/RotatingFileSink.h>


namespace Omni::Logger {

class LoggerObj {
    public:
        LoggerObj(
            const std::string& logger_name, const std::string& log_path
        )
        :   logger_name_(logger_name),
            rotating_file_sink_(
                quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
                    log_path,
                    []() {
                        quill::RotatingFileSinkConfig cfg;
                        cfg.set_open_mode('a');
                        cfg.set_rotation_max_file_size(100*1024*1024);
                        return cfg;
                    }()
                )
            )
        {
        }

        quill::Logger* create_or_get_logger() {
            return quill::Frontend::create_or_get_logger(
                logger_name_,
                std::move(rotating_file_sink_),
                quill::PatternFormatterOptions{
                    "%(time) [%(thread_id)] %(short_source_location:<28) "
                    "LOG_%(log_level:<9) %(logger:<12) %(message)",
                    "%H:%M:%S.%Qns",
                    quill::Timezone::GmtTime
                }
            );
        }

    private:
        std::string logger_name_;
        std::shared_ptr<quill::Sink> rotating_file_sink_;
};

} // namespace Omni::Logger
