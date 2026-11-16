#include "http/data/repository/response/kiotty_http_answer_rules.h"

#include "http/data/repository/server/kiotty_http_route_path_rules.h"
#include "http/domain/entity/kiotty_http_constants.h"

namespace kiotty::net::implementation
{
    namespace
    {
        constexpr int32_t ANSWER_FORBIDDEN = KIOTTY_NET_HTTP_STATUS_FORBIDDEN;
        constexpr int32_t ANSWER_NOT_ALLOWED = KIOTTY_NET_HTTP_STATUS_METHOD_NOT_ALLOWED;
        constexpr int32_t ANSWER_NO_CONTENT = KIOTTY_NET_HTTP_STATUS_NO_CONTENT;

        Answer answerWith(AnswerKind kind, int32_t status);
        Answer allowAnswerOf(int32_t status, uint32_t allowed);
        Answer staticAnswerOf(const StaticTarget& target, Span<const char> path);
        Answer memoryAnswerOf(const StaticTarget& target);
        Answer fileAnswerOf(KiottyNetHttpContentType type, Span<const char> location);
        Answer directoryAnswerOf(const StaticTarget& target, Span<const char> path);
    }

    Answer answerOf(const RouteMatch& match, Span<const char> path)
    {
        switch (match.kind)
        {
        case MatchKind::ROUTE:
            return answerWith(AnswerKind::NONE, 0);
        case MatchKind::STATIC:
            return staticAnswerOf(match.target, path);
        case MatchKind::UPGRADE:
        case MatchKind::NOT_FOUND:
            return answerWith(AnswerKind::STATUS, NOT_FOUND);
        case MatchKind::NOT_ALLOWED:
            return allowAnswerOf(ANSWER_NOT_ALLOWED, match.allowed);
        case MatchKind::OPTIONS:
            return allowAnswerOf(ANSWER_NO_CONTENT, match.allowed);
        }
        return answerWith(AnswerKind::NONE, 0);
    }

    AfterRoute afterRouteOf(const RouteOutcome& outcome)
    {
        const bool is_head_stream = outcome.is_head && outcome.is_stream;
        const bool is_working = outcome.is_work_requested && !is_head_stream;
        if (is_working) { return AfterRoute::AWAIT_WORK; }
        const bool is_orphan_stream = outcome.is_stream && !outcome.is_work_requested;
        return is_orphan_stream ? AfterRoute::REFUSE_STREAM : AfterRoute::SEND;
    }

    namespace
    {
        Answer answerWith(AnswerKind kind, int32_t status)
        {
            Answer answer{};
            answer.kind = kind;
            answer.status = status;
            return answer;
        }

        Answer allowAnswerOf(int32_t status, uint32_t allowed)
        {
            Answer answer = answerWith(AnswerKind::ALLOW, status);
            answer.allowed = allowed;
            return answer;
        }

        Answer staticAnswerOf(const StaticTarget& target, Span<const char> path)
        {
            switch (target.kind)
            {
            case StaticKind::NONE:
                return answerWith(AnswerKind::NONE, 0);
            case StaticKind::MEMORY:
                return memoryAnswerOf(target);
            case StaticKind::FILE:
                return fileAnswerOf(target.type, target.location);
            case StaticKind::DIRECTORY:
                return directoryAnswerOf(target, path);
            }
            return answerWith(AnswerKind::NONE, 0);
        }

        Answer memoryAnswerOf(const StaticTarget& target)
        {
            Answer answer = answerWith(AnswerKind::MEMORY, 0);
            answer.type = target.type;
            answer.memory = target.memory;
            return answer;
        }

        Answer fileAnswerOf(KiottyNetHttpContentType type, Span<const char> location)
        {
            Answer answer = answerWith(AnswerKind::FILE, 0);
            answer.type = type;
            const bool is_kept = answer.location.assign(location);
            if (!is_kept) { return answerWith(AnswerKind::STATUS, NOT_FOUND); }
            return answer;
        }

        Answer directoryAnswerOf(const StaticTarget& target, Span<const char> path)
        {
            const Span<const char> rest = path.skip(target.prefix.length);
            const bool is_safe = isSafeRelativePath(rest);
            if (!is_safe) { return answerWith(AnswerKind::STATUS, ANSWER_FORBIDDEN); }
            Answer answer = answerWith(AnswerKind::FILE, 0);
            answer.type = KIOTTY_NET_HTTP_CONTENT_TYPE_BY_EXTENSION;
            const bool is_started = answer.location.assign(target.location);
            const bool is_joined = is_started && answer.location.append(rest);
            if (!is_joined) { return answerWith(AnswerKind::STATUS, NOT_FOUND); }
            return answer;
        }
    }
}
