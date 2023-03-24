#include <parser/IStreamQuadIteratorSerdImpl.hpp>
#include <rdf4cpp/rdf/datatypes/rdf.hpp>
#include <fmt/format.h>
#include <cassert>
#include <cstddef>

namespace rdf4cpp::rdftools::parser {
    using CowString = ::rdf4cpp::rdf::util::CowString;
    using Borrowed = ::rdf4cpp::rdf::util::ownership_tag::Borrowed;
    using Owned = ::rdf4cpp::rdf::util::ownership_tag::Owned;

    namespace util {

        auto const escape_lexical = [](std::string_view const lexical) noexcept {
            // TODO: this is ignores UTF8
            std::stringstream out;
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
            return out.str();
        };

        /**
         * Adaptor function so that serd can read from std::istreams.
         * Matches the interface of SerdSource
         */
        static size_t
        istream_read(void *buf, [[maybe_unused]] size_t elem_size, size_t count, void *voided_self) noexcept {
            assert(elem_size == 1);

            auto *self = reinterpret_cast<std::istream *>(voided_self);
            self->read(static_cast<char *>(buf), static_cast<std::streamsize>(count));
            return self->gcount();
        }

/**
 * Adaptor function for serd to check if an std::istream is ok
 * Matches the interface of SerdStreamErrorFunc
 */
        static int istream_is_ok(void *voided_self) noexcept {
            auto *self = reinterpret_cast<std::istream *>(voided_self);
            return *self ? 0 : 1;
        }

    }  // namespace util

    std::string_view IStreamQuadIterator::Impl::node_into_string_view(SerdNode const *node) noexcept {
        return std::string_view{reinterpret_cast<char const *>(node->buf), static_cast<size_t>(node->n_bytes)};
    }

    ParsingError::Type IStreamQuadIterator::Impl::parsing_error_type_from_serd(SerdStatus const st) noexcept {
        switch (st) {
            case SerdStatus::SERD_ERR_BAD_SYNTAX:
                return ParsingError::Type::BadSyntax;
            case SerdStatus::SERD_ERR_BAD_CURIE:
                return ParsingError::Type::BadCurie;
            case SerdStatus::SERD_FAILURE:
                return ParsingError::Type::EofReached;
            case SerdStatus::SERD_ERR_ID_CLASH:
                return ParsingError::Type::BadBlankNode;
            default:
                return ParsingError::Type::Internal;
        }
    }

    nonstd::expected<CowString, SerdStatus> IStreamQuadIterator::Impl::get_bnode(SerdNode const *node) noexcept {
        try {
            return CowString{Borrowed{}, node_into_string_view(node)};
        } catch (std::runtime_error const &e) {
            // TODO: check when actual blank node validation implemented
            // NOTE: line, col not entirely accurate as this function is called after a triple was parsed
            this->last_error = ParsingError{
                    .error_type = ParsingError::Type::BadBlankNode,
                    .line = serd_reader_get_current_line(this->reader.get()),
                    .col = serd_reader_get_current_col(this->reader.get()),
                    .message = std::string{e.what()} +
                               ". note: position may not be accurate and instead point to the end of the triple."};

            return nonstd::make_unexpected(SerdStatus::SERD_ERR_BAD_SYNTAX);
        }
    }

    nonstd::expected<CowString, SerdStatus> IStreamQuadIterator::Impl::get_iri(SerdNode const *node) noexcept {
        try {
            return CowString{Owned{}, fmt::format("<{}>", node_into_string_view(node))};
        } catch (std::runtime_error const &e) {
            // TODO: check when actual iri validation implemented
            // NOTE: line, col not entirely accurate as this function is called after a triple was parsed
            this->last_error = ParsingError{
                    .error_type = ParsingError::Type::BadIri,
                    .line = serd_reader_get_current_line(this->reader.get()),
                    .col = serd_reader_get_current_col(this->reader.get()),
                    .message = std::string{e.what()} +
                               ". note: position may not be accurate and instead point to the end of the triple."};

            return nonstd::make_unexpected(SerdStatus::SERD_ERR_BAD_SYNTAX);
        }
    }

    nonstd::expected<CowString, SerdStatus>
    IStreamQuadIterator::Impl::get_prefixed_iri(SerdNode const *node) noexcept {
        auto const uri_node_view = node_into_string_view(node);

        auto const sep_pos = uri_node_view.find(':');
        if (sep_pos == std::string_view::npos) {
            return nonstd::make_unexpected(SerdStatus::SERD_ERR_BAD_CURIE);
        }

        auto const prefix = uri_node_view.substr(0, sep_pos);
        auto const suffix = uri_node_view.substr(sep_pos + 1);

        if (auto const prefix_it = this->prefixes.find(prefix); prefix_it != this->prefixes.end()) {
            try {
                return CowString{Owned{}, fmt::format("<{}{}>", prefix_it->second, suffix)};
            } catch (std::runtime_error const &e) {
                // TODO: check when actual iri validation implemented
                // NOTE: line, col not entirely accurate as this function is called after a triple was parsed
                this->last_error = ParsingError{
                        .error_type = ParsingError::Type::BadIri,
                        .line = serd_reader_get_current_line(this->reader.get()),
                        .col = serd_reader_get_current_col(this->reader.get()),
                        .message = std::string{"unable to expand curie into valid iri. "} + e.what() +
                                   ". note: position may not be accurate and instead point to the end of the triple."};

                return nonstd::make_unexpected(SerdStatus::SERD_ERR_BAD_SYNTAX);
            }
        } else {
            // NOTE: line, col not entirely accurate as this function is called after a triple was parsed
            this->last_error = ParsingError{
                    .error_type = ParsingError::Type::BadCurie,
                    .line = serd_reader_get_current_line(this->reader.get()),
                    .col = serd_reader_get_current_col(this->reader.get()),
                    .message = "unknown prefix. note: position may not be accurate and instead point to the end of the triple."};

            return nonstd::make_unexpected(SerdStatus::SERD_ERR_BAD_CURIE);
        }
    }

    nonstd::expected<CowString, SerdStatus>
    IStreamQuadIterator::Impl::get_literal(SerdNode const *literal, SerdNode const *datatype,
                                           SerdNode const *lang) noexcept {
        auto const literal_value = node_into_string_view(literal);
        nonstd::expected<std::string, SerdStatus> x;

        auto const datatype_iri = [&]() -> std::optional<nonstd::expected<CowString, SerdStatus>> {
            if (datatype != nullptr) {
                switch (datatype->type) {
                    case SerdType::SERD_CURIE:
                        return this->get_prefixed_iri(datatype);
                    case SerdType::SERD_URI:
                        return this->get_iri(datatype);
                    default:
                        assert(false);
                        __builtin_unreachable();
                }
            } else {
                return std::nullopt;
            }
        }();

        try {
            if (datatype_iri.has_value()) { // optional
                if (!datatype_iri->has_value()) {
                    return nonstd::make_unexpected(datatype_iri->error());
                }
                // remove < >
                auto const datatype_iri_v = datatype_iri->value().view().substr(1UL,
                                                                                datatype_iri->value().size() - 2UL);
                // get datatype_id
                auto const datatype_id = [&]() {
                    using namespace rdf4cpp::rdf::datatypes::registry;
                    // first look if it is fixed
                    auto found = reserved_datatype_ids.find(datatype_iri_v);
                    if (found != reserved_datatype_ids.end()) {
                        return DatatypeIDView{found->second};
                    } else { // otherwise it's stringbased
                        return DatatypeIDView{datatype_iri_v};
                    }
                }();
                // strings are printed without ^^<xsd::string>
                if (datatype_id.is_fixed() and
                    datatype_id.get_fixed() != rdf4cpp::rdf::datatypes::xsd::String::fixed_id) {
                    auto const *entry = rdf4cpp::rdf::datatypes::registry::DatatypeRegistry::get_entry(datatype_id);
                    return CowString{Owned{}, fmt::format("\"{}\"^^<{}>", entry->to_canonical_string_fptr(
                                                                  entry->factory_fptr(literal_value)),
                                                          datatype_iri_v)};
                } else {
                    return CowString{Owned{}, fmt::format("\"{}\"", util::escape_lexical(literal_value))};
                }
            } else if (lang != nullptr) {
                return CowString{Owned{}, fmt::format("\"{}\"@{}", util::escape_lexical(literal_value),
                                                      node_into_string_view(lang))};
            } else {
                return CowString{Owned{}, fmt::format("\"{}\"", util::escape_lexical(literal_value))};
            }
        } catch (std::runtime_error const &e) {
            // NOTE: line, col not entirely accurate as this function is called after a triple was parsed
            this->last_error = ParsingError{
                    .error_type = ParsingError::Type::BadLiteral,
                    .line = serd_reader_get_current_line(this->reader.get()),
                    .col = serd_reader_get_current_col(this->reader.get()),
                    .message = std::string{e.what()} +
                               ". note: position may not be accurate and instead point to the end of the triple."};

            return nonstd::make_unexpected(SerdStatus::SERD_ERR_BAD_SYNTAX);
        }
    }

    SerdStatus IStreamQuadIterator::Impl::on_error(void *voided_self, SerdError const *error) noexcept {
        auto *self = reinterpret_cast<Impl *>(voided_self);

        auto const buf_sz = vsnprintf(nullptr, 0, error->fmt, *error->args);
        std::string message;

        message.resize(buf_sz + 1);  // +1 for null-terminator
        vsnprintf(message.data(), message.size(), error->fmt, *error->args);
        message.resize(buf_sz - 1);  // drop null-terminator from vsnprintf and newline from serd

        self->last_error = ParsingError{
                .error_type = parsing_error_type_from_serd(error->status),
                .line = error->line,
                .col = error->col,
                .message = message};

        return SerdStatus::SERD_SUCCESS;
    }

    SerdStatus IStreamQuadIterator::Impl::on_base(void *voided_self, const SerdNode *uri) noexcept {
        auto *self = reinterpret_cast<Impl *>(voided_self);

        if (self->no_parse_prefixes) {
            self->last_error = ParsingError{
                    .error_type = ParsingError::Type::BadSyntax,
                    .line = serd_reader_get_current_line(self->reader.get()),
                    .col = serd_reader_get_current_col(self->reader.get()),
                    .message = "Encountered base while parsing. hint: prefix parsing is currently deactivated. note: position may not be accurate and instead point to the end of the line."};
        } else {
            self->prefixes.emplace("", node_into_string_view(uri));
        }

        return SERD_SUCCESS;
    }

    SerdStatus
    IStreamQuadIterator::Impl::on_prefix(void *voided_self, SerdNode const *name, SerdNode const *uri) noexcept {
        auto *self = reinterpret_cast<Impl *>(voided_self);

        if (self->no_parse_prefixes) {
            self->last_error = ParsingError{
                    .error_type = ParsingError::Type::BadSyntax,
                    .line = serd_reader_get_current_line(self->reader.get()),
                    .col = serd_reader_get_current_col(self->reader.get()),
                    .message = "Encountered prefix while parsing. hint: prefix parsing is currently deactivated. note: position may not be accurate and instead point to the end of the line."};
        } else {
            self->prefixes.emplace(node_into_string_view(name), node_into_string_view(uri));
        }

        return SERD_SUCCESS;
    }

    SerdStatus IStreamQuadIterator::Impl::on_stmt(void *voided_self,
                                                  SerdStatementFlags,
                                                  SerdNode const *graph,
                                                  SerdNode const *subj,
                                                  SerdNode const *pred,
                                                  SerdNode const *obj,
                                                  SerdNode const *obj_datatype,
                                                  SerdNode const *obj_lang) noexcept {

        auto *self = reinterpret_cast<Impl *>(voided_self);

        auto const graph_node = [&]() -> nonstd::expected<CowString, SerdStatus> {
            if (graph != nullptr) {
                switch (graph->type) {
                    case SERD_CURIE:
                        return self->get_prefixed_iri(graph);
                    case SERD_URI:
                        return self->get_iri(graph);
                    case SERD_BLANK:
                        return self->get_bnode(graph);
                    default:
                        return nonstd::make_unexpected(SERD_ERR_BAD_SYNTAX);
                }
            } else {
                static constexpr auto empty_graph = "";
                return CowString{Borrowed{}, std::string_view{empty_graph, 0UL}};
            }
        }();

        if (!graph_node.has_value()) {
            return graph_node.error();
        }

        auto const subj_node = [&]() -> nonstd::expected<CowString, SerdStatus> {
            switch (subj->type) {
                case SERD_CURIE:
                    return self->get_prefixed_iri(subj);
                case SERD_URI:
                    return self->get_iri(subj);
                case SERD_BLANK:
                    return self->get_bnode(subj);
                default:
                    return nonstd::make_unexpected(SERD_ERR_BAD_SYNTAX);
            }
        }();

        if (!subj_node.has_value()) {
            return subj_node.error();
        }

        auto const pred_node = [&]() -> nonstd::expected<CowString, SerdStatus> {
            switch (pred->type) {
                case SERD_CURIE:
                    return self->get_prefixed_iri(pred);
                case SERD_URI:
                    return self->get_iri(pred);
                default:
                    return nonstd::make_unexpected(SERD_ERR_BAD_SYNTAX);
            }
        }();

        if (!pred_node.has_value()) {
            return pred_node.error();
        }

        auto const obj_node = [&]() -> nonstd::expected<CowString, SerdStatus> {
            switch (obj->type) {
                case SERD_CURIE:
                    return self->get_prefixed_iri(obj);
                case SERD_LITERAL:
                    return self->get_literal(obj, obj_datatype, obj_lang);
                case SERD_BLANK:
                    return self->get_bnode(obj);
                case SERD_URI:
                    return self->get_iri(obj);
                default:
                    return nonstd::make_unexpected(SERD_ERR_BAD_SYNTAX);
            }
        }();

        if (!obj_node.has_value()) {
            return obj_node.error();
        }
        self->quad_buffer.push_back({*graph_node, *subj_node, *pred_node, *obj_node});
        return SERD_SUCCESS;
    }

    IStreamQuadIterator::Impl::Impl(std::istream &istream, ParsingFlags flags, PrefixMap prefixes) noexcept
            : istream{std::ref(istream)},
              reader{serd_reader_new(SerdSyntax::SERD_TURTLE, this, nullptr, &Impl::on_base, &Impl::on_prefix,
                                     &Impl::on_stmt, nullptr)},
              prefixes{std::move(prefixes)},
              no_parse_prefixes{flags.contains(ParsingFlag::NoParsePrefix)} {

        serd_reader_set_strict(this->reader.get(), flags.contains(ParsingFlag::Strict));
        serd_reader_set_error_sink(this->reader.get(), &Impl::on_error, this);
        serd_reader_start_source_stream(this->reader.get(), &util::istream_read, &util::istream_is_ok,
                                        &this->istream.get(), nullptr, 4096);
    }

    std::optional<nonstd::expected<StringQuad, ParsingError>> IStreamQuadIterator::Impl::next() noexcept {
        if (this->is_at_end()) [[unlikely]] {
            return std::nullopt;
        }

        while (this->quad_buffer.empty()) {
            this->last_error = std::nullopt;
            SerdStatus const st = serd_reader_read_chunk(this->reader.get());

            if (quad_buffer.empty()) {
                if (st != SerdStatus::SERD_SUCCESS) {
                    // was not able to read stmt, prefix or base

                    if (!this->last_error.has_value()) {
                        // did not receive error either => must be eof
                        this->end_flag = true;
                        return std::nullopt;  // eof reached
                    }

                    serd_reader_skip_error(this->reader.get());
                    return nonstd::make_unexpected(*this->last_error);
                } else if (this->last_error.has_value()) {
                    // non-fatal, artificially inserted error
                    return nonstd::make_unexpected(*this->last_error);
                }
            }
        }

        auto ret = this->quad_buffer.front();
        this->quad_buffer.pop_front();
        return ret;
    }

}  // namespace rdf4cpp::rdf::parser
