#include <chrono>
#include <filesystem>
#include <fstream>

#include <cxxopts.hpp>
#include <fmt/format.h>

#include <rdf4cpp/rdf/version.hpp>
#include <rdf4cpp/rdf/storage/util/tsl/sparse_set.h>
#include <rdf4cpp/rdf/parser/RDFFileParser.hpp>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <ranges>
#include <xxh3.h>

#include "rdftools_version.hpp"

/**
 * Hash algorithm with good performance in hashtables
 */
using uint64_fast_hash = rdf4cpp::rdf::storage::util::robin_hood::hash<uint64_t>;

/**
 * Hash the triple part of an rdf4cpp Quad.
 * @note Hashing happens based on rdf4cpp node handles. rdf4cpp canonized most literals before assigning an handle.
 * This deduplicates, e.g: "1.0"^^xsd:decimal and "1"^^xsd:decimal
 * @param quad the quad containing the triple part
 * @return an hash
 */
auto hash_quad(rdf4cpp::rdf::Quad const &quad) -> uint64_t {
    assert(&quad.subject() + 1 == &quad.predicate() and &quad.subject() + 2 == &quad.object());
    return XXH3_64bits(&quad.subject(), sizeof(rdf4cpp::rdf::Node) * 3UL);
};

/**
 * Destructor for std::istream holding either a std::ifstream or std::cin. std::cin is not deleted.
 */
auto istream_destructor = [](std::istream *is_ptr) {
    if (is_ptr and is_ptr != &std::cin) {
        static_cast<std::ifstream *>(is_ptr)->close();
        delete is_ptr;
    }
};

auto const quoted_lexical_into_stream = [](std::ostream &out, std::string_view const lexical) noexcept {
    // TODO: this is ignores UTF8

    out << "\"";
    for (auto const character: lexical) {
        switch (character) {
            case '\\': {
                out << R"(\\)";
                break;
            }
            case '\n': {
                out << R"(\n)";
                break;
            }
            case '\r': {
                out << R"(\r)";
                break;
            }
            case '"': {
                out << R"(\")";
                break;
            }
                [[likely]] default : {
                out << character;
                break;
            }
        }
    }
    out << "\"";
};

/**
 * Destructor for std::ostream holding either a std::ofstream or std::cin. std::cout is not deleted.
 */
auto ostream_destructor = [](std::ostream *os_ptr) {
    if (os_ptr and os_ptr != &std::cout) {
        static_cast<std::ofstream *>(os_ptr)->close();
        delete os_ptr;
    }
};

int main(int argc, char *argv[]) {
    static constexpr auto tool_name = "deduprdf";
    /*
     * Parse Commandline Arguments
     */
    cxxopts::Options options(tool_name,
                             fmt::format(
                                     "{}\nDeduplicates RDF files (TURTLE, NTRIPLE). Result is serialized in NTRIPLE on console out. Logs are written to console error.\n"
                                     "Based on {} v{}",
                                     ::rdf4cpp::rdftools::version,
                                     ::dice::rdf4cpp::name, ::dice::rdf4cpp::version));
    {
        using namespace spdlog::level;
        options.add_options()
                ("f,file", "(optional) TURTLE or NTRIPLE RDF file that should be processed.",
                 cxxopts::value<std::string>())
                ("m,limit", "(optional) Maximum number of result triples. When the limit is reached, the tool quits.",
                 cxxopts::value<size_t>())
                ("o,output", "(optional) file to write result to. The file will be overwritten.",
                 cxxopts::value<std::string>())
                ("v,version", "Version info.")
                ("h,help", "Print this help page.");
    }
    auto parsed_args = options.parse(argc, argv);
    if (parsed_args.count("help")) {
        std::cerr << options.help() << std::endl;
        exit(EXIT_SUCCESS);
    } else if (parsed_args.count("version")) {
        std::cerr << ::rdf4cpp::rdftools::version << std::endl;
        exit(EXIT_SUCCESS);
    }
    auto const limit = (parsed_args.count("limit")) ? parsed_args["limit"].as<size_t>()
                                                    : std::numeric_limits<size_t>::max();

    /*
     * Initialize logger
     */
    spdlog::set_default_logger(spdlog::stderr_color_mt(std::string{tool_name} + "_logger"));
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("%Y-%m-%dT%T.%e%z | %n | %t | %l | %v");
    spdlog::info("{} v{} based on {} v{}",
                 tool_name, ::rdf4cpp::rdftools::version,
                 ::dice::rdf4cpp::name, ::dice::rdf4cpp::version);

    /*
     * Select input from file or pipe
     */
    auto in = [&]() -> std::unique_ptr<std::istream, decltype(istream_destructor)> {

        if (parsed_args["file"].count()) {
            namespace fs = std::filesystem;
            auto const file_path = fs::path(parsed_args["file"].as<std::string>());
            // make sure that the file can be opened
            if (not fs::exists(file_path)) {
                std::cerr << file_path << " does not exist.";
                exit(EXIT_FAILURE);
            }
            auto ifs = std::unique_ptr<std::ifstream, decltype(istream_destructor)>{new std::ifstream{file_path},
                                                                                    istream_destructor};
            if (not ifs->is_open()) {
                std::cerr << "unable to open provided file " << file_path << "." << std::endl;
                exit(EXIT_FAILURE);
            }
            return ifs;
        } else if (not bool(isatty(fileno(stdin)))) { // only works on POSIX right now
            return std::unique_ptr<std::istream, decltype(istream_destructor)>{&std::cin, istream_destructor};
        } else {
            std::cerr << "Specify either an input file via '--file' or pipe input in.";
            exit(EXIT_FAILURE);
        }
    }();

    /*
     * Select output to file or pipe
     */
    auto out = [&]() -> std::unique_ptr<std::ostream, decltype(ostream_destructor)> {

        if (parsed_args["output"].count()) {
            namespace fs = std::filesystem;
            auto const file_path = fs::path(parsed_args["output"].as<std::string>());
            // make sure that the file can be opened

            auto ofs = std::unique_ptr<std::ofstream, decltype(ostream_destructor)>{
                    new std::ofstream{file_path, std::ios::binary}, ostream_destructor};
            if (not ofs->is_open()) {
                std::cerr << "Unable to open output file " << file_path << "." << std::endl;
                exit(EXIT_FAILURE);
            }
            return ofs;
        } else {
            return std::unique_ptr<std::ostream, decltype(ostream_destructor)>{&std::cout, ostream_destructor};
        }
    }();

    // terminate when the limit is reached
    size_t count = 0UL;
    auto terminate_at_limit = [&count, &limit] {
        if (++count > limit) {
            (void) std::cout.flush();
            spdlog::info("Limit of {} triples reached.", limit);
            spdlog::info("Shutdown successful.");
            exit(EXIT_SUCCESS);
        }
    };

    // hashmap for deduplication
    rdf4cpp::rdf::storage::util::tsl::sparse_set<uint64_t, uint64_fast_hash> deduplication;
    for (rdf4cpp::rdf::parser::IStreamQuadIterator qit{*in};
         qit != rdf4cpp::rdf::parser::IStreamQuadIterator{}; ++qit) {
        if (qit->has_value()) {
            auto const &quad = qit->value();
            auto const hash = hash_quad(quad);
            auto &&[_, inserted] = deduplication.insert(hash);
            if (inserted) {
                terminate_at_limit();
                std::string const object_str = [](auto obj) -> std::string {
                    if (obj.is_literal()) {
                        auto const lit = obj.as_literal();
                        if (lit.template datatype_eq<rdf4cpp::rdf::datatypes::xsd::String>()) {
                            std::stringstream sb;
                            quoted_lexical_into_stream(sb, lit.lexical_form());
                            return sb.str();
                        }
                    }
                    return static_cast<std::string>(obj);

                }(quad.object());
                (*out) << fmt::format("{} {} {} .\n",
                                      static_cast<std::string>(quad.subject()),
                                      static_cast<std::string>(quad.predicate()),
                                      object_str);
            }
        } else {
            std::stringstream sb;
            sb << qit->error();
            spdlog::warn(sb.str());
        }
    }
    out->flush();
    spdlog::info("Shutdown successful.");
    return EXIT_SUCCESS;
}

