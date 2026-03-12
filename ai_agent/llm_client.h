/*
 * llm_client.h - LLM API Client (WinHTTP)
 *
 * Part of the AIAgent reusable module.
 * Provides HTTPS communication with OpenAI-compatible LLM APIs.
 * Uses WinHTTP (Windows built-in, zero external dependencies).
 *
 * Supported providers (OpenAI-compatible):
 *   - ZhipuAI (open.bigmodel.cn)
 *   - OpenAI (api.openai.com)
 *   - DeepSeek (api.deepseek.com)
 *   - Moonshot (api.moonshot.cn)
 *   - Any OpenAI-compatible endpoint
 *
 * Usage:
 *   LLMClient client;
 *   client.configure("https://open.bigmodel.cn/api/paas/v4/chat/completions",
 *                     "your-api-key", "glm-4-flash");
 *   std::string reply = client.chat(messages, 0.7, 500);
 */

#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <string>
#include <vector>
#include <functional>
#include "json_utils.h"

namespace aiagent {

struct LLMConfig {
    std::string endpoint;   // Full API URL
    std::string apiKey;     // Bearer token
    std::string model;      // Model name
    float temperature;      // 0.0 - 1.0
    int maxTokens;          // Max response tokens
    
    LLMConfig() : temperature(0.7f), maxTokens(400) {}
};

struct LLMResponse {
    bool success;
    std::string content;    // Assistant reply text
    std::string error;      // Error message if failed
    int statusCode;         // HTTP status code
    int promptTokens;
    int completionTokens;
    
    LLMResponse() : success(false), statusCode(0), promptTokens(0), completionTokens(0) {}
};

class LLMClient {
public:
    LLMClient();
    ~LLMClient();
    
    // Configure the client
    void configure(const std::string& endpoint, const std::string& apiKey, 
                   const std::string& model);
    void configure(const LLMConfig& config);
    
    // Load config from file (key=value format)
    bool loadConfig(const std::string& filePath);
    
    // Send a chat completion request (blocking)
    LLMResponse chat(const std::vector<JsonValue>& messages);
    LLMResponse chat(const std::vector<JsonValue>& messages, 
                     float temperature, int maxTokens);
    
    // Check if configured
    bool isConfigured() const;
    
    const LLMConfig& getConfig() const { return config_; }
    
private:
    LLMConfig config_;
    
    // Build the request JSON body
    std::string buildRequestBody(const std::vector<JsonValue>& messages,
                                 float temperature, int maxTokens);
    
    // Execute HTTPS POST request via WinHTTP
    LLMResponse httpPost(const std::string& url, const std::string& body,
                         const std::string& authHeader);
    
    // Parse URL into components
    struct UrlParts {
        std::string host;
        std::string path;
        int port;
        bool isHttps;
    };
    UrlParts parseUrl(const std::string& url);
};

} // namespace aiagent

// ============================================================
// Implementation
// ============================================================
#ifdef AI_AGENT_IMPLEMENTATION
#ifndef LLM_CLIENT_IMPL_GUARD
#define LLM_CLIENT_IMPL_GUARD

#include <windows.h>
#include <winhttp.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

namespace aiagent {

LLMClient::LLMClient() {}
LLMClient::~LLMClient() {}

void LLMClient::configure(const std::string& endpoint, const std::string& apiKey,
                           const std::string& model) {
    config_.endpoint = endpoint;
    config_.apiKey = apiKey;
    config_.model = model;
}

void LLMClient::configure(const LLMConfig& config) {
    config_ = config;
}

bool LLMClient::loadConfig(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        
        // Trim whitespace
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(val.begin());
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();
        
        if (key == "API_KEY") config_.apiKey = val;
        else if (key == "API_ENDPOINT") config_.endpoint = val;
        else if (key == "MODEL_NAME") config_.model = val;
        else if (key == "TEMPERATURE") config_.temperature = (float)atof(val.c_str());
        else if (key == "MAX_TOKENS") config_.maxTokens = atoi(val.c_str());
    }
    return isConfigured();
}

bool LLMClient::isConfigured() const {
    return !config_.endpoint.empty() && !config_.apiKey.empty() && !config_.model.empty();
}

std::string LLMClient::buildRequestBody(const std::vector<JsonValue>& messages,
                                          float temperature, int maxTokens) {
    JsonValue body = JsonValue::makeObject();
    body.objVal["model"] = JsonValue::makeString(config_.model);
    body.objVal["temperature"] = JsonValue::makeNumber(temperature);
    body.objVal["max_tokens"] = JsonValue::makeNumber(maxTokens);
    
    JsonValue msgArr = JsonValue::makeArray();
    for (const auto& msg : messages) {
        msgArr.arrVal.push_back(msg);
    }
    body.objVal["messages"] = msgArr;
    
    return jsonStringify(body);
}

LLMClient::UrlParts LLMClient::parseUrl(const std::string& url) {
    UrlParts parts;
    parts.isHttps = true;
    parts.port = 443;
    
    std::string remaining = url;
    if (remaining.substr(0, 8) == "https://") {
        remaining = remaining.substr(8);
        parts.isHttps = true;
        parts.port = 443;
    } else if (remaining.substr(0, 7) == "http://") {
        remaining = remaining.substr(7);
        parts.isHttps = false;
        parts.port = 80;
    }
    
    size_t slashPos = remaining.find('/');
    if (slashPos == std::string::npos) {
        parts.host = remaining;
        parts.path = "/";
    } else {
        parts.host = remaining.substr(0, slashPos);
        parts.path = remaining.substr(slashPos);
    }
    
    // Check for port in host
    size_t colonPos = parts.host.find(':');
    if (colonPos != std::string::npos) {
        parts.port = atoi(parts.host.substr(colonPos + 1).c_str());
        parts.host = parts.host.substr(0, colonPos);
    }
    
    return parts;
}

LLMResponse LLMClient::httpPost(const std::string& url, const std::string& body,
                                 const std::string& authHeader) {
    LLMResponse response;
    UrlParts parts = parseUrl(url);
    
    // Convert strings to wide strings for WinHTTP
    auto toWide = [](const std::string& s) -> std::wstring {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
        std::wstring ws(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], len);
        return ws;
    };
    
    std::wstring wHost = toWide(parts.host);
    std::wstring wPath = toWide(parts.path);
    std::wstring wAuth = toWide("Authorization: Bearer " + authHeader + 
                                 "\r\nContent-Type: application/json");
    
    // Open WinHTTP session
    HINTERNET hSession = WinHttpOpen(L"AIAgent/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        response.error = "Failed to open WinHTTP session";
        return response;
    }
    
    // Set timeouts (connect: 10s, send: 30s, receive: 60s)
    WinHttpSetTimeouts(hSession, 10000, 10000, 30000, 60000);
    
    // Connect
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), 
                                         (INTERNET_PORT)parts.port, 0);
    if (!hConnect) {
        response.error = "Failed to connect to " + parts.host;
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    // Create request
    DWORD flags = parts.isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wPath.c_str(),
                                             NULL, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        response.error = "Failed to create HTTP request";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    // Add headers
    WinHttpAddRequestHeaders(hRequest, wAuth.c_str(), (DWORD)-1L, 
                              WINHTTP_ADDREQ_FLAG_ADD);
    
    // Send request
    BOOL bResult = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       (LPVOID)body.c_str(), (DWORD)body.size(),
                                       (DWORD)body.size(), 0);
    if (!bResult) {
        response.error = "Failed to send request";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    // Receive response
    bResult = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResult) {
        response.error = "Failed to receive response";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    // Get status code
    DWORD statusCode = 0;
    DWORD dwSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &dwSize, 
                         WINHTTP_NO_HEADER_INDEX);
    response.statusCode = (int)statusCode;
    
    // Read response body
    std::string responseBody;
    DWORD bytesAvail = 0;
    do {
        bytesAvail = 0;
        WinHttpQueryDataAvailable(hRequest, &bytesAvail);
        if (bytesAvail > 0) {
            std::vector<char> buf(bytesAvail + 1, 0);
            DWORD bytesRead = 0;
            WinHttpReadData(hRequest, buf.data(), bytesAvail, &bytesRead);
            responseBody.append(buf.data(), bytesRead);
        }
    } while (bytesAvail > 0);
    
    // Cleanup
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    // Parse response
    if (statusCode == 200) {
        JsonValue json = jsonParse(responseBody);
        response.content = json.getPath("choices[0].message.content").asString();
        response.promptTokens = (int)json.getPath("usage.prompt_tokens").asNumber();
        response.completionTokens = (int)json.getPath("usage.completion_tokens").asNumber();
        response.success = !response.content.empty();
        if (!response.success) {
            response.error = "Empty response content. Raw: " + 
                             responseBody.substr(0, 200);
        }
    } else {
        JsonValue json = jsonParse(responseBody);
        response.error = json.getPath("error.message").asString();
        if (response.error.empty()) {
            response.error = "HTTP " + std::to_string(statusCode) + ": " + 
                             responseBody.substr(0, 200);
        }
    }
    
    return response;
}

LLMResponse LLMClient::chat(const std::vector<JsonValue>& messages) {
    return chat(messages, config_.temperature, config_.maxTokens);
}

LLMResponse LLMClient::chat(const std::vector<JsonValue>& messages,
                              float temperature, int maxTokens) {
    if (!isConfigured()) {
        LLMResponse r;
        r.error = "LLM client not configured. Call configure() or loadConfig() first.";
        return r;
    }
    
    std::string body = buildRequestBody(messages, temperature, maxTokens);
    return httpPost(config_.endpoint, body, config_.apiKey);
}

} // namespace aiagent

#endif // LLM_CLIENT_IMPL_GUARD
#endif // AI_AGENT_IMPLEMENTATION
#endif // LLM_CLIENT_H
