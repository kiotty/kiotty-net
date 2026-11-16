#include "domain/usecases/http/kiotty_http_response_text.h"
#include "domain/usecases/http/kiotty_http_upload.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

using kiotty::net::implementation::checkUploadEnd;
using kiotty::net::implementation::checkUploadPiece;
using kiotty::net::implementation::chunkHeadOf;
using kiotty::net::implementation::ChunkHead;
using kiotty::net::implementation::HttpUpload;
using kiotty::net::implementation::idleUpload;
using kiotty::net::implementation::startUpload;
using kiotty::net::implementation::takeUploadPiece;
using kiotty::net::implementation::UploadMode;

namespace
{
    const KiottyNetHttpMethod PUT = KIOTTY_NET_HTTP_METHOD_PUT;
    const size_t              OVER_RESULT = static_cast<size_t>(INT32_MAX) + 1u;

    std::string textOf(const ChunkHead& head)
    {
        return std::string(head.span().data, head.span().length);
    }
}

TEST(HttpUpload, KnownLengthCountsDownAndEndsAtZero)
{
    HttpUpload upload = startUpload(PUT, 10);
    EXPECT_EQ(UploadMode::LENGTH, upload.mode);
    EXPECT_EQ(PUT, upload.method);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkUploadEnd(upload));
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, checkUploadPiece(upload, 4));
    upload = takeUploadPiece(upload, 4);
    EXPECT_EQ(6u, upload.left);
    ASSERT_EQ(KIOTTY_NET_CODE_SUCCESS, checkUploadPiece(upload, 6));
    upload = takeUploadPiece(upload, 6);
    EXPECT_EQ(0u, upload.left);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkUploadEnd(upload));
}

TEST(HttpUpload, PieceOverTheDeclaredLengthIsInvalidAndKeepsTheCount)
{
    const HttpUpload upload = startUpload(PUT, 5);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkUploadPiece(upload, 6));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkUploadPiece(upload, 5));
    EXPECT_EQ(5u, upload.left);
}

TEST(HttpUpload, ZeroLengthEndsWithoutAnyPiece)
{
    const HttpUpload upload = startUpload(PUT, 0);
    EXPECT_EQ(UploadMode::LENGTH, upload.mode);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkUploadEnd(upload));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkUploadPiece(upload, 0));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkUploadPiece(upload, 1));
}

TEST(HttpUpload, UnknownLengthIsChunkedWithoutALimit)
{
    HttpUpload upload = startUpload(KIOTTY_NET_HTTP_METHOD_POST, KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN);
    EXPECT_EQ(UploadMode::CHUNKED, upload.mode);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkUploadEnd(upload));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkUploadPiece(upload, 1000000));
    upload = takeUploadPiece(upload, 1000000);
    EXPECT_EQ(UploadMode::CHUNKED, upload.mode);
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkUploadEnd(upload));
}

TEST(HttpUpload, PieceLargerThanAResultCanCountIsInvalid)
{
    const HttpUpload chunked = startUpload(PUT, KIOTTY_NET_HTTP_BODY_LENGTH_UNKNOWN);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkUploadPiece(chunked, OVER_RESULT));
    EXPECT_EQ(KIOTTY_NET_CODE_SUCCESS, checkUploadPiece(chunked, OVER_RESULT - 1));
    const HttpUpload counted = startUpload(PUT, 10000000000ull);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkUploadPiece(counted, OVER_RESULT));
}

TEST(HttpUpload, IdleRefusesPiecesAndTheEnd)
{
    const HttpUpload upload = idleUpload();
    EXPECT_EQ(UploadMode::IDLE, upload.mode);
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkUploadPiece(upload, 0));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkUploadPiece(upload, 1));
    EXPECT_EQ(KIOTTY_NET_CODE_INVALID_ARGUMENT, checkUploadEnd(upload));
}

TEST(HttpUpload, ChunkHeadIsLowercaseHexAndLineEnd)
{
    EXPECT_EQ("1\r\n", textOf(chunkHeadOf(1)));
    EXPECT_EQ("1a2b\r\n", textOf(chunkHeadOf(0x1a2b)));
    EXPECT_EQ("7fffffff\r\n", textOf(chunkHeadOf(0x7fffffff)));
}
