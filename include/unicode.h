/*
 * ntfs-linker
 * Copyright 2015 Stroz Friedberg, LLC
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License is available at
 * <http://www.gnu.org/licenses/>.
 *
 * You can contact Stroz Friedberg by electronic and paper mail as follows:
 *
 * Stroz Friedberg, LLC
 * 32 Avenue of the Americas
 * 4th Floor
 * New York, NY, 10013
 * info@strozfriedberg.com
 */

#pragma once

typedef unsigned char byte;

template <bool LE, typename B>
size_t utf16_to_cp(const B buf, const B end, int32_t& cp) {
  if (end - buf < 2) {
    // invalid, too few bytes
    return 0;
  }

  cp = buf[LE ? 0 : 1] | (buf[LE ? 1 : 0] << 8);

  if (cp < 0xD800) {
    // direct representation
    return 2;
  }
  else if (cp < 0xDC00) {
    // found lead of UTF-16 surrogate pair
    const uint16_t lead = cp;

    if (end - buf < 4) {
      // invalid, too few bytes
      return 0;
    }

    const uint16_t trail = buf[LE ? 2 : 3] | (buf[LE ? 3 : 2] << 8);
    if (trail < 0xDC00) {
      // invalid
      return 0;
    }
    else if (trail < 0xE000) {
      // found trail of UTF-16 surrogate pair
      cp = ((lead - (0xD800 - (0x10000 >> 10))) << 10) | (trail - 0xDC00);
      return 4;
    }
    else {
      // invalid
      return 0;
    }
  }
  else if (cp < 0xE000) {
    // invalid
    return 0;
  }
  else {
    // direct representation
    return 2;
  }
}

template <typename B>
size_t cp_to_utf8(int32_t cp, B& buf) {
  if (cp < 0) {
    // too small
    return 0;
  }
  else if (cp < 0x80) {
    // one byte
    buf[0] = (byte) cp;
    return 1;
  }
  else if (cp < 0x800) {
    // two bytes
    buf[0] = 0xC0 | ((cp >> 6) & 0x1F);
    buf[1] = 0x80 | ( cp       & 0x3F);
    return 2;
  }
  else if (cp < 0xD800) {
    // three bytes
    buf[0] = 0xE0 | ((cp >> 12) & 0x0F);
    buf[1] = 0x80 | ((cp >>  6) & 0x3F);
    buf[2] = 0x80 | ( cp        & 0x3F);
    return 3;
  }
  else if (cp < 0xE000) {
    // UTF-16 surrogates, invalid
    return 0;
  }
  else if (cp < 0x10000) {
    // three bytes
    buf[0] = 0xE0 | ((cp >> 12) & 0x0F);
    buf[1] = 0x80 | ((cp >>  6) & 0x3F);
    buf[2] = 0x80 | ( cp        & 0x3F);
    return 3;
  }
  else if (cp < 0x110000) {
    // four bytes
    buf[0] = 0xF0 | ((cp >> 18) & 0x07);
    buf[1] = 0x80 | ((cp >> 12) & 0x3F);
    buf[2] = 0x80 | ((cp >>  6) & 0x3F);
    buf[3] = 0x80 | ( cp        & 0x3F);
    return 4;
  }
  else {
    // too large
    return 0;
  }
}
