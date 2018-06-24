// This file was originally part of Berkeley Softfloat. It has been modified for use in iSH.

/*============================================================================

This C source fragment is part of the Berkeley SoftFloat IEEE Floating-Point
Arithmetic Package, Release 2c, by John R. Hauser.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TOLERATE ALL LOSSES, COSTS, OR OTHER
PROBLEMS THEY INCUR DUE TO THE SOFTWARE WITHOUT RECOMPENSE FROM JOHN HAUSER OR
THE INTERNATIONAL COMPUTER SCIENCE INSTITUTE, AND WHO FURTHERMORE EFFECTIVELY
INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE INSTITUTE
(possibly via similar legal notice) AGAINST ALL LOSSES, COSTS, OR OTHER
PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE, OR
INCURRED BY ANYONE DUE TO A DERIVATIVE WORK THEY CREATE USING ANY PART OF THE
SOFTWARE.

Derivative works require also that (1) the source code for the derivative work
includes prominent notice that the work is derivative, and (2) the source code
includes prominent notice of these three paragraphs for those parts of this
code that are retained.

=============================================================================*/

/*----------------------------------------------------------------------------
| Shifts `a' right by the number of bits given in `count'.  If any nonzero
| bits are shifted off, they are "jammed" into the least significant bit of
| the result by setting the least significant bit to 1.  The value of `count'
| can be arbitrarily large; in particular, if `count' is greater than 32, the
| result will be either 0 or 1, depending on whether `a' is zero or nonzero.
| The result is stored in the location pointed to by `zPtr'.
*----------------------------------------------------------------------------*/

static inline void shift32RightJamming( uint32_t a, int16_t count, uint32_t *zPtr )
{
    uint32_t z;

    if ( count == 0 ) {
        z = a;
    }
    else if ( count < 32 ) {
        z = ( a>>count ) | ( ( a<<( ( - count ) & 31 ) ) != 0 );
    }
    else {
        z = ( a != 0 );
    }
    *zPtr = z;

}

/*----------------------------------------------------------------------------
| Shifts `a' right by the number of bits given in `count'.  If any nonzero
| bits are shifted off, they are "jammed" into the least significant bit of
| the result by setting the least significant bit to 1.  The value of `count'
| can be arbitrarily large; in particular, if `count' is greater than 64, the
| result will be either 0 or 1, depending on whether `a' is zero or nonzero.
| The result is stored in the location pointed to by `zPtr'.
*----------------------------------------------------------------------------*/

static inline void shift64RightJamming( uint64_t a, int16_t count, uint64_t *zPtr )
{
    uint64_t z;

    if ( count == 0 ) {
        z = a;
    }
    else if ( count < 64 ) {
        z = ( a>>count ) | ( ( a<<( ( - count ) & 63 ) ) != 0 );
    }
    else {
        z = ( a != 0 );
    }
    *zPtr = z;

}

/*----------------------------------------------------------------------------
| Shifts the 128-bit value formed by concatenating `a0' and `a1' right by 64
| _plus_ the number of bits given in `count'.  The shifted result is at most
| 64 nonzero bits; this is stored at the location pointed to by `z0Ptr'.  The
| bits shifted off form a second 64-bit result as follows:  The _last_ bit
| shifted off is the most-significant bit of the extra result, and the other
| 63 bits of the extra result are all zero if and only if _all_but_the_last_
| bits shifted off were all zero.  This extra result is stored in the location
| pointed to by `z1Ptr'.  The value of `count' can be arbitrarily large.
|     (This routine makes more sense if `a0' and `a1' are considered to form
| a fixed-point value with binary point between `a0' and `a1'.  This fixed-
| point value is shifted right by the number of bits given in `count', and
| the integer part of the result is returned at the location pointed to by
| `z0Ptr'.  The fractional part of the result may be slightly corrupted as
| described above, and is returned at the location pointed to by `z1Ptr'.)
*----------------------------------------------------------------------------*/

static inline void
 shift64ExtraRightJamming(
     uint64_t a0, uint64_t a1, int16_t count, uint64_t *z0Ptr, uint64_t *z1Ptr )
{
    uint64_t z0, z1;
    int8_t negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z1 = a1;
        z0 = a0;
    }
    else if ( count < 64 ) {
        z1 = ( a0<<negCount ) | ( a1 != 0 );
        z0 = a0>>count;
    }
    else {
        if ( count == 64 ) {
            z1 = a0 | ( a1 != 0 );
        }
        else {
            z1 = ( ( a0 | a1 ) != 0 );
        }
        z0 = 0;
    }
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Shifts the 128-bit value formed by concatenating `a0' and `a1' right by the
| number of bits given in `count'.  Any bits shifted off are lost.  The value
| of `count' can be arbitrarily large; in particular, if `count' is greater
| than 128, the result will be 0.  The result is broken into two 64-bit pieces
| which are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 shift128Right(
     uint64_t a0, uint64_t a1, int16_t count, uint64_t *z0Ptr, uint64_t *z1Ptr )
{
    uint64_t z0, z1;
    int8_t negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z1 = a1;
        z0 = a0;
    }
    else if ( count < 64 ) {
        z1 = ( a0<<negCount ) | ( a1>>count );
        z0 = a0>>count;
    }
    else {
        z1 = ( count < 128 ) ? ( a0>>( count & 63 ) ) : 0;
        z0 = 0;
    }
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Shifts the 128-bit value formed by concatenating `a0' and `a1' right by the
| number of bits given in `count'.  If any nonzero bits are shifted off, they
| are "jammed" into the least significant bit of the result by setting the
| least significant bit to 1.  The value of `count' can be arbitrarily large;
| in particular, if `count' is greater than 128, the result will be either
| 0 or 1, depending on whether the concatenation of `a0' and `a1' is zero or
| nonzero.  The result is broken into two 64-bit pieces which are stored at
| the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 shift128RightJamming(
     uint64_t a0, uint64_t a1, int16_t count, uint64_t *z0Ptr, uint64_t *z1Ptr )
{
    uint64_t z0, z1;
    int8_t negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z1 = a1;
        z0 = a0;
    }
    else if ( count < 64 ) {
        z1 = ( a0<<negCount ) | ( a1>>count ) | ( ( a1<<negCount ) != 0 );
        z0 = a0>>count;
    }
    else {
        if ( count == 64 ) {
            z1 = a0 | ( a1 != 0 );
        }
        else if ( count < 128 ) {
            z1 = ( a0>>( count & 63 ) ) | ( ( ( a0<<negCount ) | a1 ) != 0 );
        }
        else {
            z1 = ( ( a0 | a1 ) != 0 );
        }
        z0 = 0;
    }
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Shifts the 192-bit value formed by concatenating `a0', `a1', and `a2' right
| by 64 _plus_ the number of bits given in `count'.  The shifted result is
| at most 128 nonzero bits; these are broken into two 64-bit pieces which are
| stored at the locations pointed to by `z0Ptr' and `z1Ptr'.  The bits shifted
| off form a third 64-bit result as follows:  The _last_ bit shifted off is
| the most-significant bit of the extra result, and the other 63 bits of the
| extra result are all zero if and only if _all_but_the_last_ bits shifted off
| were all zero.  This extra result is stored in the location pointed to by
| `z2Ptr'.  The value of `count' can be arbitrarily large.
|     (This routine makes more sense if `a0', `a1', and `a2' are considered
| to form a fixed-point value with binary point between `a1' and `a2'.  This
| fixed-point value is shifted right by the number of bits given in `count',
| and the integer part of the result is returned at the locations pointed to
| by `z0Ptr' and `z1Ptr'.  The fractional part of the result may be slightly
| corrupted as described above, and is returned at the location pointed to by
| `z2Ptr'.)
*----------------------------------------------------------------------------*/

static inline void
 shift128ExtraRightJamming(
     uint64_t a0,
     uint64_t a1,
     uint64_t a2,
     int16_t count,
     uint64_t *z0Ptr,
     uint64_t *z1Ptr,
     uint64_t *z2Ptr
 )
{
    uint64_t z0, z1, z2;
    int8_t negCount = ( - count ) & 63;

    if ( count == 0 ) {
        z2 = a2;
        z1 = a1;
        z0 = a0;
    }
    else {
        if ( count < 64 ) {
            z2 = a1<<negCount;
            z1 = ( a0<<negCount ) | ( a1>>count );
            z0 = a0>>count;
        }
        else {
            if ( count == 64 ) {
                z2 = a1;
                z1 = a0;
            }
            else {
                a2 |= a1;
                if ( count < 128 ) {
                    z2 = a0<<negCount;
                    z1 = a0>>( count & 63 );
                }
                else {
                    z2 = ( count == 128 ) ? a0 : ( a0 != 0 );
                    z1 = 0;
                }
            }
            z0 = 0;
        }
        z2 |= ( a2 != 0 );
    }
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Shifts the 128-bit value formed by concatenating `a0' and `a1' left by the
| number of bits given in `count'.  Any bits shifted off are lost.  The value
| of `count' must be less than 64.  The result is broken into two 64-bit
| pieces which are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 shortShift128Left(
     uint64_t a0, uint64_t a1, int16_t count, uint64_t *z0Ptr, uint64_t *z1Ptr )
{

    *z1Ptr = a1<<count;
    *z0Ptr =
        ( count == 0 ) ? a0 : ( a0<<count ) | ( a1>>( ( - count ) & 63 ) );

}

/*----------------------------------------------------------------------------
| Shifts the 192-bit value formed by concatenating `a0', `a1', and `a2' left
| by the number of bits given in `count'.  Any bits shifted off are lost.
| The value of `count' must be less than 64.  The result is broken into three
| 64-bit pieces which are stored at the locations pointed to by `z0Ptr',
| `z1Ptr', and `z2Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 shortShift192Left(
     uint64_t a0,
     uint64_t a1,
     uint64_t a2,
     int16_t count,
     uint64_t *z0Ptr,
     uint64_t *z1Ptr,
     uint64_t *z2Ptr
 )
{
    uint64_t z0, z1, z2;
    int8_t negCount;

    z2 = a2<<count;
    z1 = a1<<count;
    z0 = a0<<count;
    if ( 0 < count ) {
        negCount = ( ( - count ) & 63 );
        z1 |= a2>>negCount;
        z0 |= a1>>negCount;
    }
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Adds the 128-bit value formed by concatenating `a0' and `a1' to the 128-bit
| value formed by concatenating `b0' and `b1'.  Addition is modulo 2^128, so
| any carry out is lost.  The result is broken into two 64-bit pieces which
| are stored at the locations pointed to by `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 add128(
     uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1, uint64_t *z0Ptr, uint64_t *z1Ptr )
{
    uint64_t z1;

    z1 = a1 + b1;
    *z1Ptr = z1;
    *z0Ptr = a0 + b0 + ( z1 < a1 );

}

/*----------------------------------------------------------------------------
| Adds the 192-bit value formed by concatenating `a0', `a1', and `a2' to the
| 192-bit value formed by concatenating `b0', `b1', and `b2'.  Addition is
| modulo 2^192, so any carry out is lost.  The result is broken into three
| 64-bit pieces which are stored at the locations pointed to by `z0Ptr',
| `z1Ptr', and `z2Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 add192(
     uint64_t a0,
     uint64_t a1,
     uint64_t a2,
     uint64_t b0,
     uint64_t b1,
     uint64_t b2,
     uint64_t *z0Ptr,
     uint64_t *z1Ptr,
     uint64_t *z2Ptr
 )
{
    uint64_t z0, z1, z2;
    int8_t carry0, carry1;

    z2 = a2 + b2;
    carry1 = ( z2 < a2 );
    z1 = a1 + b1;
    carry0 = ( z1 < a1 );
    z0 = a0 + b0;
    z1 += carry1;
    z0 += ( z1 < carry1 );
    z0 += carry0;
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Subtracts the 128-bit value formed by concatenating `b0' and `b1' from the
| 128-bit value formed by concatenating `a0' and `a1'.  Subtraction is modulo
| 2^128, so any borrow out (carry out) is lost.  The result is broken into two
| 64-bit pieces which are stored at the locations pointed to by `z0Ptr' and
| `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 sub128(
     uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1, uint64_t *z0Ptr, uint64_t *z1Ptr )
{

    *z1Ptr = a1 - b1;
    *z0Ptr = a0 - b0 - ( a1 < b1 );

}

/*----------------------------------------------------------------------------
| Subtracts the 192-bit value formed by concatenating `b0', `b1', and `b2'
| from the 192-bit value formed by concatenating `a0', `a1', and `a2'.
| Subtraction is modulo 2^192, so any borrow out (carry out) is lost.  The
| result is broken into three 64-bit pieces which are stored at the locations
| pointed to by `z0Ptr', `z1Ptr', and `z2Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 sub192(
     uint64_t a0,
     uint64_t a1,
     uint64_t a2,
     uint64_t b0,
     uint64_t b1,
     uint64_t b2,
     uint64_t *z0Ptr,
     uint64_t *z1Ptr,
     uint64_t *z2Ptr
 )
{
    uint64_t z0, z1, z2;
    int8_t borrow0, borrow1;

    z2 = a2 - b2;
    borrow1 = ( a2 < b2 );
    z1 = a1 - b1;
    borrow0 = ( a1 < b1 );
    z0 = a0 - b0;
    z0 -= ( z1 < borrow1 );
    z1 -= borrow1;
    z0 -= borrow0;
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Multiplies `a' by `b' to obtain a 128-bit product.  The product is broken
| into two 64-bit pieces which are stored at the locations pointed to by
| `z0Ptr' and `z1Ptr'.
*----------------------------------------------------------------------------*/

static inline void mul64To128( uint64_t a, uint64_t b, uint64_t *z0Ptr, uint64_t *z1Ptr )
{
    uint32_t aHigh, aLow, bHigh, bLow;
    uint64_t z0, zMiddleA, zMiddleB, z1;

    aLow = a;
    aHigh = a>>32;
    bLow = b;
    bHigh = b>>32;
    z1 = ( (uint64_t) aLow ) * bLow;
    zMiddleA = ( (uint64_t) aLow ) * bHigh;
    zMiddleB = ( (uint64_t) aHigh ) * bLow;
    z0 = ( (uint64_t) aHigh ) * bHigh;
    zMiddleA += zMiddleB;
    z0 += ( ( (uint64_t) ( zMiddleA < zMiddleB ) )<<32 ) + ( zMiddleA>>32 );
    zMiddleA <<= 32;
    z1 += zMiddleA;
    z0 += ( z1 < zMiddleA );
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Multiplies the 128-bit value formed by concatenating `a0' and `a1' by
| `b' to obtain a 192-bit product.  The product is broken into three 64-bit
| pieces which are stored at the locations pointed to by `z0Ptr', `z1Ptr', and
| `z2Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 mul128By64To192(
     uint64_t a0,
     uint64_t a1,
     uint64_t b,
     uint64_t *z0Ptr,
     uint64_t *z1Ptr,
     uint64_t *z2Ptr
 )
{
    uint64_t z0, z1, z2, more1;

    mul64To128( a1, b, &z1, &z2 );
    mul64To128( a0, b, &z0, &more1 );
    add128( z0, more1, 0, z1, &z0, &z1 );
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Multiplies the 128-bit value formed by concatenating `a0' and `a1' to the
| 128-bit value formed by concatenating `b0' and `b1' to obtain a 256-bit
| product.  The product is broken into four 64-bit pieces which are stored at
| the locations pointed to by `z0Ptr', `z1Ptr', `z2Ptr', and `z3Ptr'.
*----------------------------------------------------------------------------*/

static inline void
 mul128To256(
     uint64_t a0,
     uint64_t a1,
     uint64_t b0,
     uint64_t b1,
     uint64_t *z0Ptr,
     uint64_t *z1Ptr,
     uint64_t *z2Ptr,
     uint64_t *z3Ptr
 )
{
    uint64_t z0, z1, z2, z3;
    uint64_t more1, more2;

    mul64To128( a1, b1, &z2, &z3 );
    mul64To128( a1, b0, &z1, &more2 );
    add128( z1, more2, 0, z2, &z1, &z2 );
    mul64To128( a0, b0, &z0, &more1 );
    add128( z0, more1, 0, z1, &z0, &z1 );
    mul64To128( a0, b1, &more1, &more2 );
    add128( more1, more2, 0, z2, &more1, &z2 );
    add128( z0, z1, 0, more1, &z0, &z1 );
    *z3Ptr = z3;
    *z2Ptr = z2;
    *z1Ptr = z1;
    *z0Ptr = z0;

}

/*----------------------------------------------------------------------------
| Returns an approximation to the 64-bit integer quotient obtained by dividing
| `b' into the 128-bit value formed by concatenating `a0' and `a1'.  The
| divisor `b' must be at least 2^63.  If q is the exact quotient truncated
| toward zero, the approximation returned lies between q and q + 2 inclusive.
| If the exact quotient q is larger than 64 bits, the maximum positive 64-bit
| unsigned integer is returned.
*----------------------------------------------------------------------------*/

static uint64_t estimateDiv128To64( uint64_t a0, uint64_t a1, uint64_t b )
{
    uint64_t b0, b1;
    uint64_t rem0, rem1, term0, term1;
    uint64_t z;

    if ( b <= a0 ) return LIT64( 0xFFFFFFFFFFFFFFFF );
    b0 = b>>32;
    z = ( b0<<32 <= a0 ) ? LIT64( 0xFFFFFFFF00000000 ) : ( a0 / b0 )<<32;
    mul64To128( b, z, &term0, &term1 );
    sub128( a0, a1, term0, term1, &rem0, &rem1 );
    while ( ( (int64_t) rem0 ) < 0 ) {
        z -= LIT64( 0x100000000 );
        b1 = b<<32;
        add128( rem0, rem1, b0, b1, &rem0, &rem1 );
    }
    rem0 = ( rem0<<32 ) | ( rem1>>32 );
    z |= ( b0<<32 <= rem0 ) ? 0xFFFFFFFF : rem0 / b0;
    return z;

}

/*----------------------------------------------------------------------------
| Returns an approximation to the square root of the 32-bit significand given
| by `a'.  Considered as an integer, `a' must be at least 2^31.  If bit 0 of
| `aExp' (the least significant bit) is 1, the integer returned approximates
| 2^31*sqrt(`a'/2^31), where `a' is considered an integer.  If bit 0 of `aExp'
| is 0, the integer returned approximates 2^31*sqrt(`a'/2^30).  In either
| case, the approximation returned lies strictly within +/-2 of the exact
| value.
*----------------------------------------------------------------------------*/

static uint32_t estimateSqrt32( int16_t aExp, uint32_t a )
{
    static const uint16_t sqrtOddAdjustments[] = {
        0x0004, 0x0022, 0x005D, 0x00B1, 0x011D, 0x019F, 0x0236, 0x02E0,
        0x039C, 0x0468, 0x0545, 0x0631, 0x072B, 0x0832, 0x0946, 0x0A67
    };
    static const uint16_t sqrtEvenAdjustments[] = {
        0x0A2D, 0x08AF, 0x075A, 0x0629, 0x051A, 0x0429, 0x0356, 0x029E,
        0x0200, 0x0179, 0x0109, 0x00AF, 0x0068, 0x0034, 0x0012, 0x0002
    };
    int8_t index;
    uint32_t z;

    index = ( a>>27 ) & 15;
    if ( aExp & 1 ) {
        z = 0x4000 + ( a>>17 ) - sqrtOddAdjustments[ index ];
        z = ( ( a / z )<<14 ) + ( z<<15 );
        a >>= 1;
    }
    else {
        z = 0x8000 + ( a>>17 ) - sqrtEvenAdjustments[ index ];
        z = a / z + z;
        z = ( 0x20000 <= z ) ? 0xFFFF8000 : ( z<<15 );
        if ( z <= a ) return (uint32_t) ( ( (int32_t) a )>>1 );
    }
    return ( (uint32_t) ( ( ( (uint64_t) a )<<31 ) / z ) ) + ( z>>1 );

}

/*----------------------------------------------------------------------------
| Returns the number of leading 0 bits before the most-significant 1 bit of
| `a'.  If `a' is zero, 32 is returned.
*----------------------------------------------------------------------------*/

static int8_t countLeadingZeros32( uint32_t a )
{
    static const int8_t countLeadingZerosHigh[] = {
        8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    int8_t shiftCount;

    shiftCount = 0;
    if ( a < 0x10000 ) {
        shiftCount += 16;
        a <<= 16;
    }
    if ( a < 0x1000000 ) {
        shiftCount += 8;
        a <<= 8;
    }
    shiftCount += countLeadingZerosHigh[ a>>24 ];
    return shiftCount;

}

/*----------------------------------------------------------------------------
| Returns the number of leading 0 bits before the most-significant 1 bit of
| `a'.  If `a' is zero, 64 is returned.
*----------------------------------------------------------------------------*/

static int8_t countLeadingZeros64( uint64_t a )
{
    int8_t shiftCount;

    shiftCount = 0;
    if ( a < ( (uint64_t) 1 )<<32 ) {
        shiftCount += 32;
    }
    else {
        a >>= 32;
    }
    shiftCount += countLeadingZeros32( a );
    return shiftCount;

}

/*----------------------------------------------------------------------------
| Returns 1 if the 128-bit value formed by concatenating `a0' and `a1'
| is equal to the 128-bit value formed by concatenating `b0' and `b1'.
| Otherwise, returns 0.
*----------------------------------------------------------------------------*/

static inline bool eq128( uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1 )
{

    return ( a0 == b0 ) && ( a1 == b1 );

}

/*----------------------------------------------------------------------------
| Returns 1 if the 128-bit value formed by concatenating `a0' and `a1' is less
| than or equal to the 128-bit value formed by concatenating `b0' and `b1'.
| Otherwise, returns 0.
*----------------------------------------------------------------------------*/

static inline bool le128( uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1 )
{

    return ( a0 < b0 ) || ( ( a0 == b0 ) && ( a1 <= b1 ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the 128-bit value formed by concatenating `a0' and `a1' is less
| than the 128-bit value formed by concatenating `b0' and `b1'.  Otherwise,
| returns 0.
*----------------------------------------------------------------------------*/

static inline bool lt128( uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1 )
{

    return ( a0 < b0 ) || ( ( a0 == b0 ) && ( a1 < b1 ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the 128-bit value formed by concatenating `a0' and `a1' is
| not equal to the 128-bit value formed by concatenating `b0' and `b1'.
| Otherwise, returns 0.
*----------------------------------------------------------------------------*/

static inline bool ne128( uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1 )
{

    return ( a0 != b0 ) || ( a1 != b1 );

}

