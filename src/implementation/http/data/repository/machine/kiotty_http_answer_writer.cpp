#include "http/data/repository/machine/kiotty_http_answer_writer.h"

#include "http/data/repository/body/kiotty_http_file_body.h"
#include "http/data/repository/response/kiotty_http_response_text_builder.h"
#include "http/domain/entity/kiotty_http_constants.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr char ALLOW_HEADER_NAME[] = "Allow";

        void writeStatusAnswer(HttpSession& session, int32_t status);
        void writeAllowAnswer(HttpSession& session, const Answer& answer);
        void writeMemoryAnswer(HttpSession& session, const Answer& answer);
        void writeFileAnswer(HttpSession& session, const Answer& answer);
    }

    void writeAnswer(HttpSession& session, const Answer& answer)
    {
        switch (answer.kind)
        {
        case AnswerKind::NONE:
            return;
        case AnswerKind::STATUS:
            writeStatusAnswer(session, answer.status);
            return;
        case AnswerKind::ALLOW:
            writeAllowAnswer(session, answer);
            return;
        case AnswerKind::MEMORY:
            writeMemoryAnswer(session, answer);
            return;
        case AnswerKind::FILE:
            writeFileAnswer(session, answer);
            return;
        }
    }

    namespace
    {
        void writeStatusAnswer(HttpSession& session, int32_t status)
        {
            session.response.begin();
            (void)session.response.setStatus(status);
        }

        void writeAllowAnswer(HttpSession& session, const Answer& answer)
        {
            const AllowText allow = allowListOf(answer.allowed);
            (void)session.response.setStatus(answer.status);
            const Span<const char> name{ ALLOW_HEADER_NAME, sizeof(ALLOW_HEADER_NAME) - 1 };
            (void)session.response.addHeader(name, allow.span());
        }

        void writeMemoryAnswer(HttpSession& session, const Answer& answer)
        {
            const BodyStorage storage = BodyStorage::REFERENCE;
            (void)session.response.setBody(answer.type, answer.memory, storage);
        }

        void writeFileAnswer(HttpSession& session, const Answer& answer)
        {
            const Span<const char> location = answer.location.span();
            const KiottyNetCode attached = attachFile(session, answer.type, location);
            if (attached != KIOTTY_NET_CODE_SUCCESS) { writeStatusAnswer(session, NOT_FOUND); }
        }
    }
}
