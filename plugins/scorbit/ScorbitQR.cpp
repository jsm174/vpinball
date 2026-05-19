// Minimal, dependency-free QR -> PNG writer.
// QR encoding: vendored Nayuki qrcodegen (MIT).
// PNG: 8-bit grayscale, single IDAT using uncompressed (stored) DEFLATE
// blocks, so no zlib dependency is needed.

#include "qrcodegen/qrcodegen.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Scorbit {

static uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc = 0xFFFFFFFFu)
{
   for (size_t i = 0; i < n; ++i) {
      crc ^= p[i];
      for (int k = 0; k < 8; ++k)
         crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1) + 1));
   }
   return crc;
}

static uint32_t Adler32(const uint8_t* p, size_t n)
{
   uint32_t a = 1, b = 0;
   for (size_t i = 0; i < n; ++i) {
      a = (a + p[i]) % 65521;
      b = (b + a) % 65521;
   }
   return (b << 16) | a;
}

static void PutBE32(std::vector<uint8_t>& v, uint32_t x)
{
   v.push_back(static_cast<uint8_t>(x >> 24));
   v.push_back(static_cast<uint8_t>(x >> 16));
   v.push_back(static_cast<uint8_t>(x >> 8));
   v.push_back(static_cast<uint8_t>(x));
}

static void PutChunk(std::vector<uint8_t>& out, const char tag[4], const std::vector<uint8_t>& data)
{
   PutBE32(out, static_cast<uint32_t>(data.size()));
   const size_t crcStart = out.size();
   out.insert(out.end(), tag, tag + 4);
   out.insert(out.end(), data.begin(), data.end());
   const uint32_t crc = Crc32(out.data() + crcStart, out.size() - crcStart) ^ 0xFFFFFFFFu;
   PutBE32(out, crc);
}

bool WriteQrPng(const std::string& text, const std::string& file)
{
   std::vector<uint8_t> qr(qrcodegen_BUFFER_LEN_MAX);
   std::vector<uint8_t> tmp(qrcodegen_BUFFER_LEN_MAX);
   if (!qrcodegen_encodeText(text.c_str(), tmp.data(), qr.data(), qrcodegen_Ecc_MEDIUM,
          qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
      return false;

   const int modules = qrcodegen_getSize(qr.data());
   const int scale = 8;
   const int quiet = 4;
   const int dim = (modules + 2 * quiet) * scale;

   // Raw image: 1 filter byte per row + 1 byte/pixel (grayscale)
   std::vector<uint8_t> raw;
   raw.reserve(static_cast<size_t>(dim) * (dim + 1));
   for (int y = 0; y < dim; ++y) {
      raw.push_back(0); // filter: none
      const int my = y / scale - quiet;
      for (int x = 0; x < dim; ++x) {
         const int mx = x / scale - quiet;
         const bool dark = mx >= 0 && my >= 0 && mx < modules && my < modules
            && qrcodegen_getModule(qr.data(), mx, my);
         raw.push_back(dark ? 0x00 : 0xFF);
      }
   }

   // zlib stream: stored DEFLATE blocks
   std::vector<uint8_t> z;
   z.push_back(0x78);
   z.push_back(0x01);
   size_t off = 0;
   while (off < raw.size()) {
      const size_t n = (raw.size() - off < 65535) ? (raw.size() - off) : 65535;
      z.push_back(off + n >= raw.size() ? 1 : 0); // BFINAL
      z.push_back(static_cast<uint8_t>(n & 0xFF));
      z.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
      z.push_back(static_cast<uint8_t>(~n & 0xFF));
      z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
      z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
      off += n;
   }
   PutBE32(z, Adler32(raw.data(), raw.size()));

   std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
   std::vector<uint8_t> ihdr;
   PutBE32(ihdr, static_cast<uint32_t>(dim));
   PutBE32(ihdr, static_cast<uint32_t>(dim));
   ihdr.push_back(8); // bit depth
   ihdr.push_back(0); // grayscale
   ihdr.push_back(0);
   ihdr.push_back(0);
   ihdr.push_back(0);
   PutChunk(png, "IHDR", ihdr);
   PutChunk(png, "IDAT", z);
   PutChunk(png, "IEND", {});

   FILE* f = std::fopen(file.c_str(), "wb");
   if (!f)
      return false;
   const size_t w = std::fwrite(png.data(), 1, png.size(), f);
   std::fclose(f);
   return w == png.size();
}

}
