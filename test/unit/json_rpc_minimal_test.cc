// Minimal test to verify JSON-RPC builds
#include <gtest/gtest.h>
#include <SocketsHpp/http/common/json_rpc.h>

using namespace SocketsHpp::http::common;

TEST(MinimalJsonRpcTest, CanCreateRequest) {
    JsonRpcRequest req;
    req.id = 1;
    req.method = "test";
    EXPECT_EQ(std::get<int>(req.id), 1);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
