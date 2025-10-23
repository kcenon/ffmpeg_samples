/**
 * Subtitle Generator
 *
 * This sample demonstrates how to generate subtitle files (SRT/VTT)
 * from text input with timing information using modern C++20.
 */

#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <string_view>
#include <vector>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace {

struct SubtitleEntry {
    int index;
    double start_time;  // in seconds
    double end_time;    // in seconds
    std::string text;
};

enum class SubtitleFormat {
    SRT,    // SubRip (.srt)
    VTT,    // WebVTT (.vtt)
    ASS     // Advanced SubStation Alpha (.ass)
};

SubtitleFormat detect_format(std::string_view filename) {
    if (filename.ends_with(".srt")) return SubtitleFormat::SRT;
    if (filename.ends_with(".vtt")) return SubtitleFormat::VTT;
    if (filename.ends_with(".ass")) return SubtitleFormat::ASS;
    return SubtitleFormat::SRT;  // default
}

std::string format_time_srt(double seconds) {
    const int hours = static_cast<int>(seconds) / 3600;
    const int minutes = (static_cast<int>(seconds) % 3600) / 60;
    const int secs = static_cast<int>(seconds) % 60;
    const int millis = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);

    return std::format("{:02d}:{:02d}:{:02d},{:03d}", hours, minutes, secs, millis);
}

std::string format_time_vtt(double seconds) {
    const int hours = static_cast<int>(seconds) / 3600;
    const int minutes = (static_cast<int>(seconds) % 3600) / 60;
    const int secs = static_cast<int>(seconds) % 60;
    const int millis = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);

    return std::format("{:02d}:{:02d}:{:02d}.{:03d}", hours, minutes, secs, millis);
}

std::string format_time_ass(double seconds) {
    const int hours = static_cast<int>(seconds) / 3600;
    const int minutes = (static_cast<int>(seconds) % 3600) / 60;
    const int secs = static_cast<int>(seconds) % 60;
    const int centisecs = static_cast<int>((seconds - static_cast<int>(seconds)) * 100);

    return std::format("{:01d}:{:02d}:{:02d}.{:02d}", hours, minutes, secs, centisecs);
}

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <output> <mode> [options]\n\n", prog_name);
    std::cout << "Modes:\n";
    std::cout << "  manual   - Manually enter subtitles with timing\n";
    std::cout << "  auto     - Auto-generate from text file with duration per line\n";
    std::cout << "  template - Generate template from text file\n\n";

    std::cout << "Manual Mode:\n";
    std::cout << std::format("  {} output.srt manual\n", prog_name);
    std::cout << "  Interactive prompt for each subtitle entry\n\n";

    std::cout << "Auto Mode:\n";
    std::cout << std::format("  {} output.vtt auto <text_file> <duration_per_line>\n", prog_name);
    std::cout << "  text_file         - Input text file (one line per subtitle)\n";
    std::cout << "  duration_per_line - Duration in seconds for each line (default: 3.0)\n\n";

    std::cout << "Template Mode:\n";
    std::cout << std::format("  {} output.srt template <text_file> <start_time> <duration>\n", prog_name);
    std::cout << "  text_file   - Input text file\n";
    std::cout << "  start_time  - Start time in seconds (default: 0.0)\n";
    std::cout << "  duration    - Duration per subtitle in seconds (default: 3.0)\n\n";

    std::cout << "Supported formats: .srt (SubRip), .vtt (WebVTT), .ass (Advanced SubStation Alpha)\n";
    std::cout << "Format is auto-detected from output file extension.\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} subtitles.srt manual\n", prog_name);
    std::cout << std::format("  {} output.vtt auto script.txt 4.0\n", prog_name);
    std::cout << std::format("  {} captions.ass template dialogue.txt 10.0 2.5\n", prog_name);
}

class SubtitleGenerator {
public:
    SubtitleGenerator(std::string_view output_file)
        : output_file_(output_file)
        , format_(detect_format(output_file)) {
    }

    void generate_manual() {
        std::cout << "Manual Subtitle Entry Mode\n";
        std::cout << "Enter 'done' as text to finish.\n\n";

        int index = 1;
        while (true) {
            SubtitleEntry entry;
            entry.index = index;

            std::cout << std::format("Subtitle #{}\n", index);

            std::cout << "Start time (seconds): ";
            std::string input;
            std::getline(std::cin, input);
            if (input.empty()) break;
            entry.start_time = std::stod(input);

            std::cout << "End time (seconds): ";
            std::getline(std::cin, input);
            if (input.empty()) break;
            entry.end_time = std::stod(input);

            std::cout << "Text (can be multi-line, empty line to finish): \n";
            std::string text;
            while (true) {
                std::string line;
                std::getline(std::cin, line);
                if (line.empty() && !text.empty()) break;
                if (!text.empty()) text += "\n";
                text += line;
            }

            if (text == "done" || text.empty()) break;

            entry.text = text;
            entries_.push_back(entry);
            index++;
        }

        write_subtitles();
    }

    void generate_auto(std::string_view text_file, double duration_per_line) {
        std::ifstream input(text_file.data());
        if (!input) {
            throw std::runtime_error(std::format("Failed to open text file: {}", text_file));
        }

        std::cout << std::format("Generating subtitles from {}\n", text_file);
        std::cout << std::format("Duration per line: {:.1f} seconds\n", duration_per_line);

        int index = 1;
        double current_time = 0.0;
        std::string line;

        while (std::getline(input, line)) {
            if (line.empty()) continue;

            SubtitleEntry entry;
            entry.index = index++;
            entry.start_time = current_time;
            entry.end_time = current_time + duration_per_line;
            entry.text = line;

            entries_.push_back(entry);
            current_time += duration_per_line;
        }

        std::cout << std::format("Generated {} subtitle entries\n", entries_.size());
        write_subtitles();
    }

    void generate_template(std::string_view text_file, double start_time, double duration) {
        std::ifstream input(text_file.data());
        if (!input) {
            throw std::runtime_error(std::format("Failed to open text file: {}", text_file));
        }

        std::cout << std::format("Generating template from {}\n", text_file);
        std::cout << std::format("Start time: {:.1f}s, Duration: {:.1f}s per line\n",
                                start_time, duration);

        int index = 1;
        double current_time = start_time;
        std::string line;

        while (std::getline(input, line)) {
            if (line.empty()) continue;

            SubtitleEntry entry;
            entry.index = index++;
            entry.start_time = current_time;
            entry.end_time = current_time + duration;
            entry.text = line;

            entries_.push_back(entry);
            current_time += duration;
        }

        std::cout << std::format("Generated {} subtitle entries\n", entries_.size());
        write_subtitles();
    }

private:
    void write_subtitles() {
        std::ofstream output(output_file_);
        if (!output) {
            throw std::runtime_error(std::format("Failed to create output file: {}", output_file_));
        }

        switch (format_) {
            case SubtitleFormat::SRT:
                write_srt(output);
                break;
            case SubtitleFormat::VTT:
                write_vtt(output);
                break;
            case SubtitleFormat::ASS:
                write_ass(output);
                break;
        }

        std::cout << std::format("\nSubtitle file created: {}\n", output_file_);
        std::cout << std::format("Format: {}\n", format_name());
        std::cout << std::format("Total entries: {}\n", entries_.size());
    }

    void write_srt(std::ofstream& output) {
        for (const auto& entry : entries_) {
            output << entry.index << "\n";
            output << format_time_srt(entry.start_time) << " --> "
                   << format_time_srt(entry.end_time) << "\n";
            output << entry.text << "\n\n";
        }
    }

    void write_vtt(std::ofstream& output) {
        output << "WEBVTT\n\n";

        for (const auto& entry : entries_) {
            output << entry.index << "\n";
            output << format_time_vtt(entry.start_time) << " --> "
                   << format_time_vtt(entry.end_time) << "\n";
            output << entry.text << "\n\n";
        }
    }

    void write_ass(std::ofstream& output) {
        // ASS header
        output << "[Script Info]\n";
        output << "Title: Generated Subtitles\n";
        output << "ScriptType: v4.00+\n";
        output << "WrapStyle: 0\n";
        output << "PlayResX: 1920\n";
        output << "PlayResY: 1080\n";
        output << "ScaledBorderAndShadow: yes\n\n";

        output << "[V4+ Styles]\n";
        output << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
               << "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
               << "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
               << "Alignment, MarginL, MarginR, MarginV, Encoding\n";
        output << "Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,"
               << "0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n\n";

        output << "[Events]\n";
        output << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

        for (const auto& entry : entries_) {
            // Replace newlines with \N for ASS format
            std::string text = entry.text;
            size_t pos = 0;
            while ((pos = text.find('\n', pos)) != std::string::npos) {
                text.replace(pos, 1, "\\N");
                pos += 2;
            }

            output << std::format("Dialogue: 0,{},{},Default,,0,0,0,,{}\n",
                                format_time_ass(entry.start_time),
                                format_time_ass(entry.end_time),
                                text);
        }
    }

    std::string_view format_name() const {
        switch (format_) {
            case SubtitleFormat::SRT: return "SubRip (SRT)";
            case SubtitleFormat::VTT: return "WebVTT";
            case SubtitleFormat::ASS: return "Advanced SubStation Alpha (ASS)";
        }
        return "Unknown";
    }

    std::string output_file_;
    SubtitleFormat format_;
    std::vector<SubtitleEntry> entries_;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string output = argv[1];
        const std::string mode = argv[2];

        SubtitleGenerator generator(output);

        if (mode == "manual") {
            generator.generate_manual();
        }
        else if (mode == "auto") {
            if (argc < 4) {
                std::cerr << "Error: auto mode requires text_file argument\n";
                print_usage(argv[0]);
                return 1;
            }
            const std::string text_file = argv[3];
            const double duration = argc > 4 ? std::stod(argv[4]) : 3.0;
            generator.generate_auto(text_file, duration);
        }
        else if (mode == "template") {
            if (argc < 4) {
                std::cerr << "Error: template mode requires text_file argument\n";
                print_usage(argv[0]);
                return 1;
            }
            const std::string text_file = argv[3];
            const double start_time = argc > 4 ? std::stod(argv[4]) : 0.0;
            const double duration = argc > 5 ? std::stod(argv[5]) : 3.0;
            generator.generate_template(text_file, start_time, duration);
        }
        else {
            std::cerr << std::format("Error: Unknown mode '{}'\n", mode);
            print_usage(argv[0]);
            return 1;
        }

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
