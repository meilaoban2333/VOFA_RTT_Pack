/*********************************************************************
*                    SEGGER Microcontroller GmbH                     *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*            (c) 1995 - 2021 SEGGER Microcontroller GmbH             *
*                                                                    *
*       www.segger.com     Support: support@segger.com               *
*                                                                    *
**********************************************************************
*                                                                    *
*       SEGGER RTT * Real Time Transfer for embedded targets         *
*                                                                    *
**********************************************************************
*                                                                    *
* All rights reserved.                                               *
*                                                                    *
* SEGGER strongly recommends to not make any changes                 *
* to or modify the source code of this software in order to stay     *
* compatible with the RTT protocol and J-Link.                       *
*                                                                    *
* Redistribution and use in source and binary forms, with or         *
* without modification, are permitted provided that the following    *
* condition is met:                                                  *
*                                                                    *
* o Redistributions of source code must retain the above copyright   *
*   notice, this condition and the following disclaimer.             *
*                                                                    *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND             *
* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,        *
* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF           *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE           *
* DISCLAIMED. IN NO EVENT SHALL SEGGER Microcontroller BE LIABLE FOR *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR           *
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  *
* OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;    *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF      *
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT          *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE  *
* USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH   *
* DAMAGE.                                                            *
*                                                                    *
**********************************************************************
*                                                                    *
*       RTT version: 8.64                                           *
*                                                                    *
**********************************************************************

---------------------------END-OF-HEADER------------------------------
File    : SEGGER_RTT_printf.c
Purpose : Replacement for printf to write formatted data via RTT
Revision: $Rev: 17697 $
----------------------------------------------------------------------
*/
#include "SEGGER_RTT.h"
#include "SEGGER_RTT_Conf.h"

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

#ifndef SEGGER_RTT_PRINTF_BUFFER_SIZE
  #define SEGGER_RTT_PRINTF_BUFFER_SIZE (64)
#endif

#include <stdlib.h>
#include <stdarg.h>


#define FORMAT_FLAG_LEFT_JUSTIFY   (1u << 0)
#define FORMAT_FLAG_PAD_ZERO       (1u << 1)
#define FORMAT_FLAG_PRINT_SIGN     (1u << 2)
#define FORMAT_FLAG_ALTERNATE      (1u << 3)

/*********************************************************************
*
*       Types
*
**********************************************************************
*/

typedef struct {
  char*     pBuffer;
  unsigned  BufferSize;
  unsigned  Cnt;

  int   ReturnValue;

  unsigned RTTBufferIndex;
} SEGGER_RTT_PRINTF_DESC;

/*********************************************************************
*
*       Function prototypes
*
**********************************************************************
*/

/*********************************************************************
*
*       Static code
*
**********************************************************************
*/
/*********************************************************************
*
*       _StoreChar
*/
static void _StoreChar(SEGGER_RTT_PRINTF_DESC * p, char c) {
  unsigned Cnt;

  Cnt = p->Cnt;
  if ((Cnt + 1u) <= p->BufferSize) {
    *(p->pBuffer + Cnt) = c;
    p->Cnt = Cnt + 1u;
    p->ReturnValue++;
  }
  //
  // Write part of string, when the buffer is full
  //
  if (p->Cnt == p->BufferSize) {
    if (SEGGER_RTT_Write(p->RTTBufferIndex, p->pBuffer, p->Cnt) != p->Cnt) {
      p->ReturnValue = -1;
    } else {
      p->Cnt = 0u;
    }
  }
}

/*********************************************************************
*
*       _PrintUnsigned
*/
static void _PrintUnsigned(SEGGER_RTT_PRINTF_DESC * pBufferDesc, unsigned v, unsigned Base, unsigned NumDigits, unsigned FieldWidth, unsigned FormatFlags) {
  static const char _aV2C[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
  unsigned Div;
  unsigned Digit;
  unsigned Number;
  unsigned Width;
  char c;

  Number = v;
  Digit = 1u;
  //
  // Get actual field width
  //
  Width = 1u;
  while (Number >= Base) {
    Number = (Number / Base);
    Width++;
  }
  if (NumDigits > Width) {
    Width = NumDigits;
  }
  //
  // Print leading chars if necessary
  //
  if ((FormatFlags & FORMAT_FLAG_LEFT_JUSTIFY) == 0u) {
    if (FieldWidth != 0u) {
      if (((FormatFlags & FORMAT_FLAG_PAD_ZERO) == FORMAT_FLAG_PAD_ZERO) && (NumDigits == 0u)) {
        c = '0';
      } else {
        c = ' ';
      }
      while ((FieldWidth != 0u) && (Width < FieldWidth)) {
        FieldWidth--;
        _StoreChar(pBufferDesc, c);
        if (pBufferDesc->ReturnValue < 0) {
          break;
        }
      }
    }
  }
  if (pBufferDesc->ReturnValue >= 0) {
    //
    // Compute Digit.
    // Loop until Digit has the value of the highest digit required.
    // Example: If the output is 345 (Base 10), loop 2 times until Digit is 100.
    //
    while (1) {
      if (NumDigits > 1u) {       // User specified a min number of digits to print? => Make sure we loop at least that often, before checking anything else (> 1 check avoids problems with NumDigits being signed / unsigned)
        NumDigits--;
      } else {
        Div = v / Digit;
        if (Div < Base) {        // Is our divider big enough to extract the highest digit from value? => Done
          break;
        }
      }
      Digit *= Base;
    }
    //
    // Output digits
    //
    do {
      Div = v / Digit;
      v -= Div * Digit;
      _StoreChar(pBufferDesc, _aV2C[Div]);
      if (pBufferDesc->ReturnValue < 0) {
        break;
      }
      Digit /= Base;
    } while (Digit);
    //
    // Print trailing spaces if necessary
    //
    if ((FormatFlags & FORMAT_FLAG_LEFT_JUSTIFY) == FORMAT_FLAG_LEFT_JUSTIFY) {
      if (FieldWidth != 0u) {
        while ((FieldWidth != 0u) && (Width < FieldWidth)) {
          FieldWidth--;
          _StoreChar(pBufferDesc, ' ');
          if (pBufferDesc->ReturnValue < 0) {
            break;
          }
        }
      }
    }
  }
}

/*********************************************************************
*
*       _PrintInt
*/
static void _PrintInt(SEGGER_RTT_PRINTF_DESC * pBufferDesc, int v, unsigned Base, unsigned NumDigits, unsigned FieldWidth, unsigned FormatFlags) {
  unsigned Width;
  int Number;

  Number = (v < 0) ? -v : v;

  //
  // Get actual field width
  //
  Width = 1u;
  while (Number >= (int)Base) {
    Number = (Number / (int)Base);
    Width++;
  }
  if (NumDigits > Width) {
    Width = NumDigits;
  }
  if ((FieldWidth > 0u) && ((v < 0) || ((FormatFlags & FORMAT_FLAG_PRINT_SIGN) == FORMAT_FLAG_PRINT_SIGN))) {
    FieldWidth--;
  }

  //
  // Print leading spaces if necessary
  //
  if ((((FormatFlags & FORMAT_FLAG_PAD_ZERO) == 0u) || (NumDigits != 0u)) && ((FormatFlags & FORMAT_FLAG_LEFT_JUSTIFY) == 0u)) {
    if (FieldWidth != 0u) {
      while ((FieldWidth != 0u) && (Width < FieldWidth)) {
        FieldWidth--;
        _StoreChar(pBufferDesc, ' ');
        if (pBufferDesc->ReturnValue < 0) {
          break;
        }
      }
    }
  }
  //
  // Print sign if necessary
  //
  if (pBufferDesc->ReturnValue >= 0) {
    if (v < 0) {
      v = -v;
      _StoreChar(pBufferDesc, '-');
    } else if ((FormatFlags & FORMAT_FLAG_PRINT_SIGN) == FORMAT_FLAG_PRINT_SIGN) {
      _StoreChar(pBufferDesc, '+');
    } else {

    }
    if (pBufferDesc->ReturnValue >= 0) {
      //
      // Print leading zeros if necessary
      //
      if (((FormatFlags & FORMAT_FLAG_PAD_ZERO) == FORMAT_FLAG_PAD_ZERO) && ((FormatFlags & FORMAT_FLAG_LEFT_JUSTIFY) == 0u) && (NumDigits == 0u)) {
        if (FieldWidth != 0u) {
          while ((FieldWidth != 0u) && (Width < FieldWidth)) {
            FieldWidth--;
            _StoreChar(pBufferDesc, '0');
            if (pBufferDesc->ReturnValue < 0) {
              break;
            }
          }
        }
      }
      if (pBufferDesc->ReturnValue >= 0) {
        //
        // Print number without sign
        //
        _PrintUnsigned(pBufferDesc, (unsigned)v, Base, NumDigits, FieldWidth, FormatFlags);
      }
    }
  }
}

/*********************************************************************
*
*       _PrintString
*
*  功能
*    按字段宽度/左对齐标志输出一个字符串。
*    供 _PrintFloat 打印 NaN / Inf 标记时使用。
*/
static void _PrintString(SEGGER_RTT_PRINTF_DESC * pBufferDesc, const char * s, unsigned FieldWidth, unsigned FormatFlags) {
  unsigned Width;
  unsigned i;

  Width = 0u;
  while (s[Width] != '\0') {
    Width++;
  }
  if ((FormatFlags & FORMAT_FLAG_LEFT_JUSTIFY) == 0u) {
    for (i = Width; (i < FieldWidth) && (pBufferDesc->ReturnValue >= 0); i++) {
      _StoreChar(pBufferDesc, ' ');
    }
  }
  for (i = 0u; (i < Width) && (pBufferDesc->ReturnValue >= 0); i++) {
    _StoreChar(pBufferDesc, s[i]);
  }
  if ((FormatFlags & FORMAT_FLAG_LEFT_JUSTIFY) == FORMAT_FLAG_LEFT_JUSTIFY) {
    for (i = Width; (i < FieldWidth) && (pBufferDesc->ReturnValue >= 0); i++) {
      _StoreChar(pBufferDesc, ' ');
    }
  }
}

/*********************************************************************
*
*       _PrintFloat
*
*  功能
*    以定点形式打印 double（即 "%f"）。
*    SEGGER 原版实现完全不支持 %f，这是本工程补充的。
*
*  说明
*    (1) 只做定点，不支持科学计数法：嵌入式日志看的是可读量程，
*        %e/%g 代码体积代价大得多。
*    (2) 整数部分拆进 32 位变量，故可打印范围为 ±4294967295。
*        超出该范围以及 NaN/Inf，统一打印标记而不是静默回绕。
*    (3) 精度默认 6 位（与标准 printf 一致），并钳位到 9 位，
*        保证 10^精度 仍在 32 位内。
*/
static void _PrintFloat(SEGGER_RTT_PRINTF_DESC * pBufferDesc, double v, unsigned Precision, unsigned FieldWidth, unsigned FormatFlags) {
  unsigned Scale;
  unsigned i;
  unsigned IntPart;
  unsigned FracPart;
  unsigned Width;
  unsigned Neg;
  double   Rounded;

  //
  // NaN / Inf 没有定点表示形式。用 v 与自身比较来判断 NaN，
  // 这样不必引入 <math.h>。
  //
  if (v != v) {
    _PrintString(pBufferDesc, "NaN", FieldWidth, FormatFlags);
    return;
  }
  Neg = 0u;
  if (v < 0.0) {
    Neg = 1u;
    v   = -v;
  }
  if (v > 4294967295.0) {
    _PrintString(pBufferDesc, Neg ? "-Inf" : "Inf", FieldWidth, FormatFlags);
    return;
  }
  //
  // 钳位精度，保证 Scale = 10^Precision 不超出 32 位。
  //
  if (Precision > 9u) {
    Precision = 9u;
  }
  Scale = 1u;
  for (i = 0u; i < Precision; i++) {
    Scale *= 10u;
  }
  //
  // 先按目标精度四舍五入，再拆整数/小数两部分。
  // 先舍入可避免 ".999..." 截断后整数部分出错。
  //
  Rounded = v + (0.5 / (double)Scale);
  if (Rounded > 4294967295.0) {
    _PrintString(pBufferDesc, Neg ? "-Inf" : "Inf", FieldWidth, FormatFlags);
    return;
  }
  IntPart  = (unsigned)Rounded;
  FracPart = (unsigned)((Rounded - (double)IntPart) * (double)Scale);
  //
  // 手工算出实际打印宽度，以便自行处理字段宽度补齐：
  // 数值分两段输出，_PrintUnsigned 无法代劳。
  //
  Width = 1u;
  for (i = IntPart; i >= 10u; i /= 10u) {
    Width++;
  }
  if (Precision > 0u) {
    Width += 1u + Precision;            // '.' plus the fractional digits
  }
  if ((Neg != 0u) || ((FormatFlags & FORMAT_FLAG_PRINT_SIGN) == FORMAT_FLAG_PRINT_SIGN)) {
    Width++;
  }
  //
  // 前导补齐（仅右对齐时）。
  //
  if ((FormatFlags & FORMAT_FLAG_LEFT_JUSTIFY) == 0u) {
    char cPad = ((FormatFlags & FORMAT_FLAG_PAD_ZERO) == FORMAT_FLAG_PAD_ZERO) ? '0' : ' ';
    //
    // 用 '0' 补齐时，符号必须先于补的零输出，
    // 否则 "%08.2f" 打印 -1.5 会变成 "0000-1.50"。
    //
    if (cPad == '0') {
      if (Neg != 0u) {
        _StoreChar(pBufferDesc, '-');
      } else if ((FormatFlags & FORMAT_FLAG_PRINT_SIGN) == FORMAT_FLAG_PRINT_SIGN) {
        _StoreChar(pBufferDesc, '+');
      } else {

      }
      Neg         = 0u;
      FormatFlags = FormatFlags & ~FORMAT_FLAG_PRINT_SIGN;
    }
    while ((Width < FieldWidth) && (pBufferDesc->ReturnValue >= 0)) {
      _StoreChar(pBufferDesc, cPad);
      FieldWidth--;
    }
  }
  //
  // 符号位。
  //
  if (Neg != 0u) {
    _StoreChar(pBufferDesc, '-');
  } else if ((FormatFlags & FORMAT_FLAG_PRINT_SIGN) == FORMAT_FLAG_PRINT_SIGN) {
    _StoreChar(pBufferDesc, '+');
  } else {

  }
  //
  // 先整数部分，再小数部分（按精度高位补零）。
  //
  _PrintUnsigned(pBufferDesc, IntPart, 10u, 0u, 0u, 0u);
  if (Precision > 0u) {
    _StoreChar(pBufferDesc, '.');
    _PrintUnsigned(pBufferDesc, FracPart, 10u, Precision, 0u, 0u);
  }
  //
  // 尾部补齐（左对齐时）。
  //
  if ((FormatFlags & FORMAT_FLAG_LEFT_JUSTIFY) == FORMAT_FLAG_LEFT_JUSTIFY) {
    while ((Width < FieldWidth) && (pBufferDesc->ReturnValue >= 0)) {
      _StoreChar(pBufferDesc, ' ');
      FieldWidth--;
    }
  }
}

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/
/*********************************************************************
*
*       SEGGER_RTT_vprintf
*
*  Function description
*    Stores a formatted string in SEGGER RTT control block.
*    This data is read by the host.
*
*  Parameters
*    BufferIndex  Index of "Up"-buffer to be used. (e.g. 0 for "Terminal")
*    sFormat      Pointer to format string
*    pParamList   Pointer to the list of arguments for the format string
*
*  Return values
*    >= 0:  Number of bytes which have been stored in the "Up"-buffer.
*     < 0:  Error
*/
int SEGGER_RTT_vprintf(unsigned BufferIndex, const char * sFormat, va_list * pParamList) {
  char c;
  SEGGER_RTT_PRINTF_DESC BufferDesc;
  int v;
  unsigned char PrecisionSet;
  unsigned Precision;
  unsigned FormatFlags;
  unsigned FieldWidth;
  char acBuffer[SEGGER_RTT_PRINTF_BUFFER_SIZE];

  BufferDesc.pBuffer        = acBuffer;
  BufferDesc.BufferSize     = SEGGER_RTT_PRINTF_BUFFER_SIZE;
  BufferDesc.Cnt            = 0u;
  BufferDesc.RTTBufferIndex = BufferIndex;
  BufferDesc.ReturnValue    = 0;

  do {
    c = *sFormat;
    sFormat++;
    if (c == 0u) {
      break;
    }
    if (c == '%') {
      //
      // Filter out flags
      //
      FormatFlags = 0u;
      v = 1;
      do {
        c = *sFormat;
        switch (c) {
        case '-': FormatFlags |= FORMAT_FLAG_LEFT_JUSTIFY; sFormat++; break;
        case '0': FormatFlags |= FORMAT_FLAG_PAD_ZERO;     sFormat++; break;
        case '+': FormatFlags |= FORMAT_FLAG_PRINT_SIGN;   sFormat++; break;
        case '#': FormatFlags |= FORMAT_FLAG_ALTERNATE;    sFormat++; break;
        default:  v = 0; break;
        }
      } while (v);
      //
      // filter out field with
      //
      FieldWidth = 0u;
      do {
        c = *sFormat;
        if ((c < '0') || (c > '9')) {
          break;
        }
        sFormat++;
        FieldWidth = (FieldWidth * 10u) + ((unsigned)c - '0');
      } while (1);

      //
      // Filter out precision (number of digits to display)
      //
      PrecisionSet = 0;
      Precision = 0u;
      c = *sFormat;
      if (c == '.') {
        sFormat++;
        if (*sFormat == '*') {
          sFormat++;
          PrecisionSet = 1;
          Precision = va_arg(*pParamList, int);
        } else {
          do {
            c = *sFormat;
            if ((c < '0') || (c > '9')) {
              break;
            }
            PrecisionSet = 1;
            sFormat++;
            Precision = Precision * 10u + ((unsigned)c - '0');
          } while (1);
        }
      }
      //
      // Filter out length modifier
      //
      c = *sFormat;
      do {
        if ((c == 'l') || (c == 'h')) {
          sFormat++;
          c = *sFormat;
        } else {
          break;
        }
      } while (1);
      //
      // Handle specifiers
      //
      switch (c) {
      case 'c': {
        char c0;
        v = va_arg(*pParamList, int);
        c0 = (char)v;
        _StoreChar(&BufferDesc, c0);
        break;
      }
      case 'd':
        v = va_arg(*pParamList, int);
        _PrintInt(&BufferDesc, v, 10u, Precision, FieldWidth, FormatFlags);
        break;
      case 'u':
        v = va_arg(*pParamList, int);
        _PrintUnsigned(&BufferDesc, (unsigned)v, 10u, Precision, FieldWidth, FormatFlags);
        break;
      case 'x':
      case 'X':
        v = va_arg(*pParamList, int);
        _PrintUnsigned(&BufferDesc, (unsigned)v, 16u, Precision, FieldWidth, FormatFlags);
        break;
      case 's':
        {
          const char * s = va_arg(*pParamList, const char *);
          if (s == NULL) {
            s = "(NULL)";     // Print (NULL) instead of crashing or breaking, as it is more informative to the user.
            PrecisionSet = 0; // Make sure (NULL) is printed, even when precision was set.
          }
          do {
            c = *s;
            s++;
            if (c == '\0') {
              break;
            }
            if ((PrecisionSet != 0) && (Precision == 0)) {
              break;
            }
            _StoreChar(&BufferDesc, c);
            Precision--;
          } while (BufferDesc.ReturnValue >= 0);
        }
        break;
      case 'f':
      case 'F':
        {
          //
          // 变参传递中 float 会被提升为 double，
          // 所以即使调用方传的是 float，这里也必须按 double 取。
          //
          double d = va_arg(*pParamList, double);
          _PrintFloat(&BufferDesc, d, (PrecisionSet != 0) ? Precision : 6u, FieldWidth, FormatFlags);
        }
        break;
      case 'p':
        v = va_arg(*pParamList, int);
        _PrintUnsigned(&BufferDesc, (unsigned)v, 16u, 8u, 8u, 0u);
        break;
      case '%':
        _StoreChar(&BufferDesc, '%');
        break;
      default:
        break;
      }
      sFormat++;
    } else {
      _StoreChar(&BufferDesc, c);
    }
  } while (BufferDesc.ReturnValue >= 0);

  if (BufferDesc.ReturnValue > 0) {
    //
    // Write remaining data, if any
    //
    if (BufferDesc.Cnt != 0u) {
      SEGGER_RTT_Write(BufferIndex, acBuffer, BufferDesc.Cnt);
    }
    BufferDesc.ReturnValue += (int)BufferDesc.Cnt;
  }
  return BufferDesc.ReturnValue;
}

/*********************************************************************
*
*       SEGGER_RTT_printf
*
*  Function description
*    Stores a formatted string in SEGGER RTT control block.
*    This data is read by the host.
*
*  Parameters
*    BufferIndex  Index of "Up"-buffer to be used. (e.g. 0 for "Terminal")
*    sFormat      Pointer to format string, followed by the arguments for conversion
*
*  Return values
*    >= 0:  Number of bytes which have been stored in the "Up"-buffer.
*     < 0:  Error
*
*  Notes
*    (1) Conversion specifications have following syntax:
*          %[flags][FieldWidth][.Precision]ConversionSpecifier
*    (2) Supported flags:
*          -: Left justify within the field width
*          +: Always print sign extension for signed conversions
*          0: Pad with 0 instead of spaces. Ignored when using '-'-flag or precision
*        Supported conversion specifiers:
*          c: Print the argument as one char
*          d: Print the argument as a signed integer
*          u: Print the argument as an unsigned integer
*          x: Print the argument as an hexadecimal integer
*          s: Print the string pointed to by the argument
*          p: Print the argument as an 8-digit hexadecimal integer. (Argument shall be a pointer to void.)
*/
int SEGGER_RTT_printf(unsigned BufferIndex, const char * sFormat, ...) {
  int r;
  va_list ParamList;

  va_start(ParamList, sFormat);
  r = SEGGER_RTT_vprintf(BufferIndex, sFormat, &ParamList);
  va_end(ParamList);
  return r;
}
/*************************** End of file ****************************/
