#include "common.h"
#include <filesystem>
#include <algorithm>

namespace B2SLegacy
{

// Helper function for case-insensitive character comparison
constexpr inline char cLower(char c)
{
   if (c >= 'A' && c <= 'Z')
      c ^= 32; // ASCII convention
   return c;
}

bool StrCompareNoCase(const string& strA, const string& strB)
{
   return strA.length() == strB.length()
      && std::equal(strA.begin(), strA.end(), strB.begin(),
         [](char a, char b) { return cLower(a) == cLower(b); });
}

string find_case_insensitive_file_path(const string &szPath)
{
   auto fn = [&](auto& self, const string& s) -> string {
      std::filesystem::path p = std::filesystem::path(s).lexically_normal();
      std::error_code ec;

      if (std::filesystem::exists(p, ec))
         return p.string();

      auto parent = p.parent_path();
      string base;
      if (parent.empty() || parent == p) {
         base = ".";
      } else {
         base = self(self, parent.string());
         if (base.empty())
            return string();
      }

      for (auto& ent : std::filesystem::directory_iterator(base, ec)) {
         if (!ec && StrCompareNoCase(ent.path().filename().string(), p.filename().string())) {
            auto found = ent.path().string();
            // Note: case insensitive file match
            return found;
         }
      }

      return string();
   };

   string result = fn(fn, szPath);
   if (!result.empty()) {
      std::filesystem::path p = std::filesystem::absolute(result);
      return p.string();
   }
   return string();
}

// Base64 decoding (copied from existing B2S plugin)
static const string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

static inline bool is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

vector<unsigned char> base64_decode(const string &encoded_string) {
  int in_len = encoded_string.size();
  int i = 0;
  int j = 0;
  int in = 0;
  unsigned char char_array_4[4], char_array_3[3];

  vector<unsigned char> ret;

  while (in_len-- && ( encoded_string[in] != '=') && is_base64(encoded_string[in])) {
    char_array_4[i++] = encoded_string[in]; in++;
    if (i ==4) {
      for (i = 0; i <4; i++)
        char_array_4[i] = base64_chars.find(char_array_4[i]);

      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; (i < 3); i++)
        ret.push_back(char_array_3[i]);
      i = 0;
    }
  }

  if (i) {
    for (j = i; j <4; j++)
      char_array_4[j] = 0;

    for (j = 0; j <4; j++)
      char_array_4[j] = base64_chars.find(char_array_4[j]);

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
  }

  return ret;
}

// VPX texture functions - these will be set up during plugin loading
static VPXPluginAPI* vpxApi = nullptr;

void SetVPXAPI(VPXPluginAPI* api) {
   vpxApi = api;
}

VPXTexture CreateTexture(uint8_t* rawData, int size)
{
   if (vpxApi)
      return vpxApi->CreateTexture(rawData, size);
   return nullptr;
}

VPXTextureInfo* GetTextureInfo(VPXTexture texture)
{
   if (vpxApi)
      return vpxApi->GetTextureInfo(texture);
   else
      return nullptr;
}

void DeleteTexture(VPXTexture texture)
{
   if (vpxApi)
      vpxApi->DeleteTexture(texture);
}

// Initialize global player instance for plugin context
Player* g_pplayer = nullptr;

// Initialize logger stubs
LoggerStub PLOGI;
LoggerStub PLOGW;
LoggerStub PLOGE;

// VPinball app stub removed - use VPX API instead

bool string_starts_with_case_insensitive(const string& str, const string& prefix)
{
   if (prefix.length() > str.length()) 
      return false;
   return std::equal(prefix.begin(), prefix.end(), str.begin(),
      [](char a, char b) { return cLower(a) == cLower(b); });
}

int string_to_int(const string& str, int defaultValue)
{
   try {
      return std::stoi(str);
   } catch (const std::exception&) {
      return defaultValue;
   }
}

string TitleAndPathFromFilename(const string& filename)
{
   // Extract directory path from filename
   size_t lastSlash = filename.find_last_of("/\\");
   if (lastSlash == string::npos) {
      return "./";
   }
   return filename.substr(0, lastSlash + 1);
}

bool is_string_numeric(const string& str)
{
   if (str.empty()) return false;
   for (char c : str) {
      if (!std::isdigit(c)) return false;
   }
   return true;
}

}