#ifndef GEMUSIC_NETWORK_NETEASE_CRYPTO_H
#define GEMUSIC_NETWORK_NETEASE_CRYPTO_H

#include <string>

#include <nlohmann/json.hpp>

namespace gemusic::network {

// ============================================================
// 网易云音乐 Weapi 加密算法
//
// 加密流程：
//   1. 将请求参数序列化为 JSON 字符串
//   2. 生成 16 字节随机 secretKey（十六进制小写字符）
//   3. 用 presetKey 对 JSON 字符串进行 AES-128-CBC 加密，Base64 编码
//   4. 再用 secretKey 对步骤 3 的 Base64 结果进行 AES-128-CBC 加密，Base64 编码 → params 字段
//   5. 用 RSA 公钥（无填充，PKCS#1 格式）对 secretKey 逆序后加密 → Hex 编码 → encSecKey 字段
//   6. POST 请求体格式：params=<base64>&encSecKey=<hex>
//   7. POST 路径需为 /weapi/ 路径（原始路径中的 /api/ 替换为 /weapi/）
//
// 硬编码常量来自网易云逆向分析（go-musicfox/netease-music）
// ============================================================

// Weapi 加密结果，包含 POST 请求体中的两个字段
struct WeapiPayload {
    std::string params;       // AES 双重加密后的参数（Base64）
    std::string enc_sec_key;  // RSA 加密的 secretKey（Hex）

    // 将两个字段拼接为 URL 编码的 POST 请求体字符串
    // 格式：params=<value>&encSecKey=<value>
    [[nodiscard]] auto ToPostBody() const -> std::string;
};

// 对 JSON 参数进行 Weapi 加密
// 参数: params - 待加密的请求参数（JSON 对象）
// 返回: WeapiPayload，包含 params 和 encSecKey 两个字段
auto EncryptWeapi(const nlohmann::json& params) -> WeapiPayload;

}  // namespace gemusic::network

#endif  // GEMUSIC_NETWORK_NETEASE_CRYPTO_H
