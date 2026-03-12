#include "gemusic/network/netease_crypto.h"

#include <algorithm>
#include <array>
#include <random>
#include <vector>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

namespace gemusic::network {

// ============================================================
// 硬编码加密常量（来自网易云逆向分析，go-musicfox/netease-music）
// ============================================================

// AES 第一次加密使用的固定预设密钥（16 字节）
static constexpr std::string_view kPresetKey = "0CoJUm6Qyw8W8jud";

// AES 初始化向量（16 字节，两次加密共用）
static constexpr std::string_view kIv = "0102030405060708";

// 网易云 RSA 公钥（PKCS#8 SubjectPublicKeyInfo 格式，PEM 编码）
// 1024 位模数，用于加密 secretKey 的逆序版本
// 公钥来源：api-enhanced（NeteaseCloudMusicApiEnhanced/api-enhanced）util/crypto.js
// Base64 按标准 64 字符换行，确保 OpenSSL PEM_read_bio_PUBKEY 正确解析
static constexpr std::string_view kPublicKeyPem =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDgtQn2JZ34ZC28NWYpAUd98iZ3\n"
    "7BUrX/aKzmFbt7clFSs6sXqHauqKWqdtLkF2KexO40H1YTX8z2lSgBBOAxLsvakl\n"
    "V8k4cBFK9snQXE9/DDaFt6Rr7iVZMldczhC0JNgTz+SHXT6CBHuX3e9SdB1Ua44o\n"
    "ncaTWz7OBGLbCiK45wIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

// Base62 字符集（用于生成随机 secretKey，避免特殊字符）
static constexpr std::string_view kBase62Chars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// ============================================================
// 内部工具函数
// ============================================================

// 生成 16 字节随机 secretKey（从 base62 字符集中随机选取）
// 使用 std::mt19937 确保跨平台的随机性
static auto GenerateSecretKey() -> std::string {
    // 使用随机设备初始化 Mersenne Twister 生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, kBase62Chars.size() - 1);

    std::string key;
    key.reserve(16);
    for (int i = 0; i < 16; ++i) {
        key += kBase62Chars[dist(gen)];
    }
    return key;
}

// AES-128-CBC 加密，返回 Base64 编码的密文
// 参数:
//   plaintext - 待加密的明文字符串
//   key       - 加密密钥（必须为 16 字节）
//   iv        - 初始化向量（必须为 16 字节）
// 返回: Base64 编码的密文（无换行）
static auto AesCbcEncryptBase64(std::string_view plaintext, std::string_view key,
                                std::string_view iv) -> std::string {
    // 初始化 OpenSSL AES-128-CBC 加密上下文
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();

    EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                       reinterpret_cast<const unsigned char*>(key.data()),
                       reinterpret_cast<const unsigned char*>(iv.data()));

    // 输出缓冲区：最大长度为 输入长度 + 2 个 AES 块（PKCS#7 填充需要额外空间）
    const int max_out_len = static_cast<int>(plaintext.size()) + 32;
    std::vector<unsigned char> ciphertext(static_cast<size_t>(max_out_len));

    int out_len1 = 0;
    int out_len2 = 0;

    // 加密主体数据
    EVP_EncryptUpdate(ctx, ciphertext.data(), &out_len1,
                      reinterpret_cast<const unsigned char*>(plaintext.data()),
                      static_cast<int>(plaintext.size()));

    // 处理最后一块（自动添加 PKCS#7 填充）
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + out_len1, &out_len2);
    EVP_CIPHER_CTX_free(ctx);

    const int total_len = out_len1 + out_len2;

    // 将密文 Base64 编码（不插入换行符）
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    // BIO_FLAGS_BASE64_NO_NL：禁止 Base64 输出中的换行符
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_push(b64, mem);
    BIO_write(b64, ciphertext.data(), total_len);
    BIO_flush(b64);

    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(mem, &bptr);

    // 在释放 BIO 链之前复制数据
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);  // 同时释放 b64 和 mem

    return result;
}

// RSA 公钥无填充加密（用于加密 secretKey）
// 算法：将 secretKey（16 字节）右对齐填入 128 字节缓冲区（左补零），
//       再用 RSA-1024 公钥进行无填充（RAW）加密
// 返回: 密文的 16 进制小写字符串（256 个字符）
static auto RsaEncryptNoPadding(std::string_view secret_key) -> std::string {
    // 从 PEM 字符串加载公钥
    BIO* bio = BIO_new_mem_buf(kPublicKeyPem.data(), static_cast<int>(kPublicKeyPem.size()));
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (pkey == nullptr) {
        return "";
    }

    // RSA-1024 模数为 128 字节
    // 将 secretKey 右对齐填入 128 字节缓冲区（左侧填充零字节）
    // 这与 JavaScript 的 Buffer.alloc(128); secretKey.copy(buf, 128 - secretKey.length) 等价
    constexpr size_t kRsaKeyBytes = 128;
    std::array<unsigned char, kRsaKeyBytes> plaintext{};
    plaintext.fill(0);

    const size_t key_len = std::min(secret_key.size(), kRsaKeyBytes);
    const size_t start_offset = kRsaKeyBytes - key_len;
    for (size_t i = 0; i < key_len; ++i) {
        plaintext[start_offset + i] = static_cast<unsigned char>(secret_key[i]);
    }

    // 使用 EVP 接口进行 RSA 无填充加密
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    EVP_PKEY_encrypt_init(ctx);
    // RSA_NO_PADDING = 3：无填充模式，输入必须与密钥模数等长
    EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_NO_PADDING);

    // 获取所需输出缓冲区大小
    size_t out_len = 0;
    EVP_PKEY_encrypt(ctx, nullptr, &out_len, plaintext.data(), kRsaKeyBytes);

    std::vector<unsigned char> ciphertext(out_len);
    EVP_PKEY_encrypt(ctx, ciphertext.data(), &out_len, plaintext.data(), kRsaKeyBytes);

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    // 将密文字节序列转换为 16 进制小写字符串
    static constexpr std::string_view kHexChars = "0123456789abcdef";
    std::string hex;
    hex.reserve(out_len * 2);
    for (size_t i = 0; i < out_len; ++i) {
        hex += kHexChars[(ciphertext[i] >> 4U) & 0xFU];
        hex += kHexChars[ciphertext[i] & 0xFU];
    }

    return hex;
}

// URL 编码（将非 unreserved 字符编码为 %XX 格式）
// 参数: str - 待编码的字符串
// 返回: URL 编码后的字符串
static auto UrlEncode(std::string_view str) -> std::string {
    std::string result;
    result.reserve(str.size() * 3);
    static constexpr std::string_view kHexChars = "0123456789ABCDEF";
    for (const auto byte : str) {
        const auto c = static_cast<unsigned char>(byte);
        // RFC 3986 unreserved 字符：字母、数字、'-', '_', '.', '~' 直接保留
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            result += static_cast<char>(c);
        } else {
            result += '%';
            result += kHexChars[(c >> 4U) & 0xFU];
            result += kHexChars[c & 0xFU];
        }
    }
    return result;
}

// ============================================================
// 公共接口实现
// ============================================================

auto WeapiPayload::ToPostBody() const -> std::string {
    // 构造 application/x-www-form-urlencoded 格式的 POST 请求体
    return "params=" + UrlEncode(params) + "&encSecKey=" + UrlEncode(enc_sec_key);
}

auto EncryptWeapi(const nlohmann::json& params) -> WeapiPayload {
    // 步骤 1：将请求参数序列化为紧凑 JSON 字符串
    const std::string json_str = params.dump();

    // 步骤 2：生成 16 字节随机 secretKey（base62 字符集）
    const std::string secret_key = GenerateSecretKey();

    // 步骤 3：第一次 AES-128-CBC 加密
    //         用固定 presetKey 加密 JSON 字符串，输出 Base64
    const std::string first_encrypted = AesCbcEncryptBase64(json_str, kPresetKey, kIv);

    // 步骤 4：第二次 AES-128-CBC 加密
    //         用随机 secretKey 加密步骤 3 的 Base64 输出，再次 Base64 编码 → params 字段
    const std::string params_encrypted = AesCbcEncryptBase64(first_encrypted, secret_key, kIv);

    // 步骤 5：RSA 加密 secretKey 的逆序版本 → Hex → encSecKey 字段
    // 参考 api-enhanced crypto.js：encSecKey = rsaEncrypt(secretKey.split('').reverse().join(''),
    // publicKey) 注意：params 第二次 AES 用正序 secret_key，encSecKey 的 RSA 加密用逆序
    // reversed_key
    const std::string reversed_key(secret_key.rbegin(), secret_key.rend());
    const std::string enc_sec_key = RsaEncryptNoPadding(reversed_key);

    return WeapiPayload{params_encrypted, enc_sec_key};
}

}  // namespace gemusic::network
