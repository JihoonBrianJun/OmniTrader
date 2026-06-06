#pragma once

#include <string>
#include <memory>
#include <iostream>

#include <quill/Frontend.h>
#include <quill/core/FrontendOptions.h>
#include <quill/sinks/RotatingFileSink.h>
#include <quill/CsvWriter.h>


namespace Omni::Logger {

template<typename T>
class CsvLogger {
    public:
        CsvLogger(
            const std::string& logger_name, const std::string& log_path
        )
        :   rotating_file_sink_(
                quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
                    log_path,
                    []() {
                        quill::RotatingFileSinkConfig cfg;
                        cfg.set_open_mode('a');
                        cfg.set_rotation_max_file_size(100*1024*1024);
                        return cfg;
                    }()
                )
            ),
            csv_writer_(
                std::make_unique<quill::CsvWriter<T, quill::FrontendOptions>>(
                    logger_name, rotating_file_sink_
                )
            )
        {
        }

        template<typename... Args>
        void write_log(Args&&... args) {
            try {
                csv_writer_->append_row(std::forward<Args>(args)...);
            } catch (const std::exception& /*e*/) {
            }
        }

    private:
        std::shared_ptr<quill::Sink> rotating_file_sink_;
        std::unique_ptr<quill::CsvWriter<T, quill::FrontendOptions>> csv_writer_;
};

} // namespace Omni::Logger
