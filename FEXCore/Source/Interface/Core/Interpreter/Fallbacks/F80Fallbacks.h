#pragma once
#include "Common/SoftFloat.h"
#include "Common/SoftFloat-3e/softfloat.h"

#include "Interface/Core/Interpreter/Fallbacks/FallbackOpHandler.h"
#include "Interface/IR/IR.h"

namespace FEXCore::CPU {
FEXCORE_PRESERVE_ALL_ATTR static softfloat_state SoftFloatStateFromFCW(uint16_t FCW, bool Force80BitPrecision = false) {
  softfloat_state State {};
  State.detectTininess = softfloat_tininess_afterRounding;
  State.exceptionFlags = 0;
  State.roundingPrecision = 80;

  if (!Force80BitPrecision) {
    auto PC = (FCW >> 8) & 3;
    switch (PC) {
    case 0: State.roundingPrecision = 32; break;
    case 2: State.roundingPrecision = 64; break;
    case 3: State.roundingPrecision = 80; break;
    case 1: LOGMAN_MSG_A_FMT("Invalid x87 precision mode, {}", PC);
    }
  }

  auto RC = (FCW >> 10) & 3;
  switch (RC) {
  case 0: State.roundingMode = softfloat_round_near_even; break;
  case 1: State.roundingMode = softfloat_round_min; break;
  case 2: State.roundingMode = softfloat_round_max; break;
  case 3: State.roundingMode = softfloat_round_minMag; break;
  }

  return State;
}

template<>
struct OpHandlers<IR::OP_F80CVTTO> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle4(uint16_t FCW, float src) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return X80SoftFloat(&State, src);
  }

  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle8(uint16_t FCW, double src) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return X80SoftFloat(&State, src);
  }
};

template<>
struct OpHandlers<IR::OP_F80CMP> {
  FEXCORE_PRESERVE_ALL_ATTR static uint64_t handle(uint16_t FCW, X80SoftFloat Src1, X80SoftFloat Src2) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);

    bool eq, lt, nan;
    uint64_t ResultFlags = 0;

    X80SoftFloat::FCMP(&State, Src1, Src2, &eq, &lt, &nan);
    if (lt) {
      ResultFlags |= (1 << IR::FCMP_FLAG_LT);
    }
    if (nan) {
      ResultFlags |= (1 << IR::FCMP_FLAG_UNORDERED);
    }
    if (eq) {
      ResultFlags |= (1 << IR::FCMP_FLAG_EQ);
    }
    return ResultFlags;
  }
};

template<>
struct OpHandlers<IR::OP_F80CVT> {
  FEXCORE_PRESERVE_ALL_ATTR static float handle4(uint16_t FCW, X80SoftFloat src) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return src.ToF32(&State);
  }

  FEXCORE_PRESERVE_ALL_ATTR static double handle8(uint16_t FCW, X80SoftFloat src) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return src.ToF64(&State);
  }
};

template<>
struct OpHandlers<IR::OP_F80CVTINT> {
  FEXCORE_PRESERVE_ALL_ATTR static int16_t handle2(uint16_t FCW, X80SoftFloat src) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return src.ToI16(&State);
  }

  FEXCORE_PRESERVE_ALL_ATTR static int32_t handle4(uint16_t FCW, X80SoftFloat src) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return src.ToI32(&State);
  }

  FEXCORE_PRESERVE_ALL_ATTR static int64_t handle8(uint16_t FCW, X80SoftFloat src) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return src.ToI64(&State);
  }

  FEXCORE_PRESERVE_ALL_ATTR static int16_t handle2t(uint16_t FCW, X80SoftFloat src) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    auto rv = extF80_to_i32(&State, src, softfloat_round_minMag, false);

    if (rv > INT16_MAX || rv < INT16_MIN) {
      ///< Indefinite value for 16-bit conversions.
      return INT16_MIN;
    } else {
      return rv;
    }
  }

  FEXCORE_PRESERVE_ALL_ATTR static int32_t handle4t(uint16_t FCW, X80SoftFloat src) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return extF80_to_i32(&State, src, softfloat_round_minMag, false);
  }

  FEXCORE_PRESERVE_ALL_ATTR static int64_t handle8t(uint16_t FCW, X80SoftFloat src) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return extF80_to_i64(&State, src, softfloat_round_minMag, false);
  }
};

template<>
struct OpHandlers<IR::OP_F80CVTTOINT> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle2(uint16_t FCW, int16_t src) {
    return src;
  }

  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle4(uint16_t FCW, int32_t src) {
    return src;
  }
};

template<>
struct OpHandlers<IR::OP_F80ROUND> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1) {
    softfloat_state State = SoftFloatStateFromFCW(FCW, true);
    return X80SoftFloat::FRNDINT(&State, Src1);
  }
};

template<>
struct OpHandlers<IR::OP_F80F2XM1> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1) {
    softfloat_state State = SoftFloatStateFromFCW(FCW, true);
    return X80SoftFloat::F2XM1(&State, Src1);
  }
};

template<>
struct OpHandlers<IR::OP_F80TAN> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1) {
    softfloat_state State = SoftFloatStateFromFCW(FCW, true);
    return X80SoftFloat::FTAN(&State, Src1);
  }
};

template<>
struct OpHandlers<IR::OP_F80SQRT> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return X80SoftFloat::FSQRT(&State, Src1);
  }
};

template<>
struct OpHandlers<IR::OP_F80SIN> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1) {
    softfloat_state State = SoftFloatStateFromFCW(FCW, true);
    return X80SoftFloat::FSIN(&State, Src1);
  }
};

template<>
struct OpHandlers<IR::OP_F80COS> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1) {
    softfloat_state State = SoftFloatStateFromFCW(FCW, true);
    return X80SoftFloat::FCOS(&State, Src1);
  }
};

template<>
struct OpHandlers<IR::OP_F80XTRACT_EXP> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1) {
    return X80SoftFloat::FXTRACT_EXP(Src1);
  }
};

template<>
struct OpHandlers<IR::OP_F80XTRACT_SIG> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1) {
    return X80SoftFloat::FXTRACT_SIG(Src1);
  }
};

template<>
struct OpHandlers<IR::OP_F80ADD> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1, X80SoftFloat Src2) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return X80SoftFloat::FADD(&State, Src1, Src2);
  }
};

template<>
struct OpHandlers<IR::OP_F80SUB> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1, X80SoftFloat Src2) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return X80SoftFloat::FSUB(&State, Src1, Src2);
  }
};

template<>
struct OpHandlers<IR::OP_F80MUL> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1, X80SoftFloat Src2) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return X80SoftFloat::FMUL(&State, Src1, Src2);
  }
};

template<>
struct OpHandlers<IR::OP_F80DIV> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1, X80SoftFloat Src2) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    return X80SoftFloat::FDIV(&State, Src1, Src2);
  }
};

template<>
struct OpHandlers<IR::OP_F80FYL2X> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1, X80SoftFloat Src2) {
    softfloat_state State = SoftFloatStateFromFCW(FCW, true);
    return X80SoftFloat::FYL2X(&State, Src1, Src2);
  }
};

template<>
struct OpHandlers<IR::OP_F80ATAN> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1, X80SoftFloat Src2) {
    softfloat_state State = SoftFloatStateFromFCW(FCW, true);
    return X80SoftFloat::FATAN(&State, Src1, Src2);
  }
};

template<>
struct OpHandlers<IR::OP_F80FPREM1> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1, X80SoftFloat Src2) {
    softfloat_state State = SoftFloatStateFromFCW(FCW, true);
    return X80SoftFloat::FREM1(&State, Src1, Src2);
  }
};

template<>
struct OpHandlers<IR::OP_F80FPREM> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1, X80SoftFloat Src2) {
    softfloat_state State = SoftFloatStateFromFCW(FCW, true);
    return X80SoftFloat::FREM(&State, Src1, Src2);
  }
};

template<>
struct OpHandlers<IR::OP_F80SCALE> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1, X80SoftFloat Src2) {
    softfloat_state State = SoftFloatStateFromFCW(FCW, true);
    return X80SoftFloat::FSCALE(&State, Src1, Src2);
  }
};

template<>
struct OpHandlers<IR::OP_F64SIN> {
  static double handle(uint16_t FCW, double src) {
    return sin(src);
  }
};

template<>
struct OpHandlers<IR::OP_F64COS> {
  static double handle(uint16_t FCW, double src) {
    return cos(src);
  }
};

template<>
struct OpHandlers<IR::OP_F64TAN> {
  static double handle(uint16_t FCW, double src) {
    return tan(src);
  }
};

template<>
struct OpHandlers<IR::OP_F64F2XM1> {
  static double handle(uint16_t FCW, double src) {
    return exp2(src) - 1.0;
  }
};

template<>
struct OpHandlers<IR::OP_F64ATAN> {
  static double handle(uint16_t FCW, double src1, double src2) {
    return atan2(src1, src2);
  }
};

template<>
struct OpHandlers<IR::OP_F64FPREM> {
  static double handle(uint16_t FCW, double src1, double src2) {
    return fmod(src1, src2);
  }
};

template<>
struct OpHandlers<IR::OP_F64FPREM1> {
  static double handle(uint16_t FCW, double src1, double src2) {
    return remainder(src1, src2);
  }
};

template<>
struct OpHandlers<IR::OP_F64FYL2X> {
  static double handle(uint16_t FCW, double src1, double src2) {
    return src2 * log2(src1);
  }
};

template<>
struct OpHandlers<IR::OP_F64SCALE> {
  static double handle(uint16_t FCW, double src1, double src2) {
    if (src1 == 0.0) { // src1 might be +/- zero
      return src1;     // this will return negative or positive zero if when appropriate
    }
    double trun = trunc(src2);
    return src1 * exp2(trun);
  }
};

template<>
struct OpHandlers<IR::OP_F80BCDSTORE> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src1) {
    softfloat_state State = SoftFloatStateFromFCW(FCW);
    bool Negative = Src1.Sign;

    Src1 = X80SoftFloat::FRNDINT(&State, Src1);

    // Clear the Sign bit
    Src1.Sign = 0;

    uint64_t Tmp = Src1.ToI64(&State);
    X80SoftFloat Rv;
    uint8_t* BCD = reinterpret_cast<uint8_t*>(&Rv);
    memset(BCD, 0, 10);

    for (size_t i = 0; i < 9; ++i) {
      if (Tmp == 0) {
        // Nothing left? Just leave
        break;
      }
      // Extract the lower 100 values
      uint8_t Digit = Tmp % 100;

      // Now divide it for the next iteration
      Tmp /= 100;

      uint8_t UpperNibble = Digit / 10;
      uint8_t LowerNibble = Digit % 10;

      // Now store the BCD
      BCD[i] = (UpperNibble << 4) | LowerNibble;
    }

    // Set negative flag once converted to x87
    BCD[9] = Negative ? 0x80 : 0;

    return Rv;
  }
};

template<>
struct OpHandlers<IR::OP_F80BCDLOAD> {
  FEXCORE_PRESERVE_ALL_ATTR static X80SoftFloat handle(uint16_t FCW, X80SoftFloat Src) {
    uint8_t* Src1 = reinterpret_cast<uint8_t*>(&Src);
    uint64_t BCD {};
    // We walk through each uint8_t and pull out the BCD encoding
    // Each 4bit split is a digit
    // Only 0-9 is supported, A-F results in undefined data
    // | 4 bit     | 4 bit    |
    // | 10s place | 1s place |
    // EG 0x48 = 48
    // EG 0x4847 = 4847
    // This gives us an 18digit value encoded in BCD
    // The last byte lets us know if it negative or not
    for (size_t i = 0; i < 9; ++i) {
      uint8_t Digit = Src1[8 - i];
      // First shift our last value over
      BCD *= 100;

      // Add the tens place digit
      BCD += (Digit >> 4) * 10;

      // Add the ones place digit
      BCD += Digit & 0xF;
    }

    // Set negative flag once converted to x87
    bool Negative = Src1[9] & 0x80;
    X80SoftFloat Tmp;

    Tmp = BCD;
    Tmp.Sign = Negative;
    return Tmp;
  }
};

} // namespace FEXCore::CPU
