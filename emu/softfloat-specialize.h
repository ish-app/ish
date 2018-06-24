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
| Underflow tininess-detection mode, statically initialized to default value.
| (The declaration in `softfloat.h' must match the `int8_t' type here.)
*----------------------------------------------------------------------------*/
int8_t float_detect_tininess = float_tininess_after_rounding;

/*----------------------------------------------------------------------------
| Raises the exceptions specified by `flags'.  Floating-point traps can be
| defined here if desired.  It is currently not possible for such a trap to
| substitute a result value.  If traps are not implemented, this routine
| should be simply `float_exception_flags |= flags;'.
*----------------------------------------------------------------------------*/

void float_raise( int8_t flags )
{

    float_exception_flags |= flags;

}

/*----------------------------------------------------------------------------
| Internal canonical NaN format.
*----------------------------------------------------------------------------*/
typedef struct {
    bool sign;
    uint64_t signExp, signif;
} commonNaNT;

/*----------------------------------------------------------------------------
| The pattern for a default generated single-precision NaN.
*----------------------------------------------------------------------------*/
#define float32_default_nan 0xFFFFFFFF

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is a NaN;
| otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float32_is_nan( float32 a )
{

    return ( 0xFF000000 < (uint32_t) ( a<<1 ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float32_is_signaling_nan( float32 a )
{

    return ( ( ( a>>22 ) & 0x1FF ) == 0x1FE ) && ( a & 0x003FFFFF );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point NaN
| `a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT float32ToCommonNaN( float32 a )
{
    commonNaNT z;

    if ( float32_is_signaling_nan( a ) ) float_raise( float_flag_invalid );
    z.sign = a>>31;
    z.signif = 0;
    z.signExp = ( (uint64_t) a )<<41;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the single-
| precision floating-point format.
*----------------------------------------------------------------------------*/

static float32 commonNaNToFloat32( commonNaNT a )
{

    return ( ( (uint32_t) a.sign )<<31 ) | 0x7FC00000 | ( a.signExp>>41 );

}

/*----------------------------------------------------------------------------
| Takes two single-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static float32 propagateFloat32NaN( float32 a, float32 b )
{
    bool aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    aIsNaN = float32_is_nan( a );
    aIsSignalingNaN = float32_is_signaling_nan( a );
    bIsNaN = float32_is_nan( b );
    bIsSignalingNaN = float32_is_signaling_nan( b );
    a |= 0x00400000;
    b |= 0x00400000;
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid );
    if ( aIsNaN ) {
        return ( aIsSignalingNaN & bIsNaN ) ? b : a;
    }
    else {
        return b;
    }

}

/*----------------------------------------------------------------------------
| The pattern for a default generated double-precision NaN.
*----------------------------------------------------------------------------*/
#define float64_default_nan LIT64( 0xFFFFFFFFFFFFFFFF )

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is a NaN;
| otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float64_is_nan( float64 a )
{

    return ( LIT64( 0xFFE0000000000000 ) < (uint64_t) ( a<<1 ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is a signaling
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float64_is_signaling_nan( float64 a )
{

    return
           ( ( ( a>>51 ) & 0xFFF ) == 0xFFE )
        && ( a & LIT64( 0x0007FFFFFFFFFFFF ) );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point NaN
| `a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT float64ToCommonNaN( float64 a )
{
    commonNaNT z;

    if ( float64_is_signaling_nan( a ) ) float_raise( float_flag_invalid );
    z.sign = a>>63;
    z.signif = 0;
    z.signExp = a<<12;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the double-
| precision floating-point format.
*----------------------------------------------------------------------------*/

static float64 commonNaNToFloat64( commonNaNT a )
{

    return
          ( ( (uint64_t) a.sign )<<63 )
        | LIT64( 0x7FF8000000000000 )
        | ( a.signExp>>12 );

}

/*----------------------------------------------------------------------------
| Takes two double-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static float64 propagateFloat64NaN( float64 a, float64 b )
{
    bool aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    aIsNaN = float64_is_nan( a );
    aIsSignalingNaN = float64_is_signaling_nan( a );
    bIsNaN = float64_is_nan( b );
    bIsSignalingNaN = float64_is_signaling_nan( b );
    a |= LIT64( 0x0008000000000000 );
    b |= LIT64( 0x0008000000000000 );
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid );
    if ( aIsNaN ) {
        return ( aIsSignalingNaN & bIsNaN ) ? b : a;
    }
    else {
        return b;
    }

}

#ifdef FLOATX80

/*----------------------------------------------------------------------------
| The pattern for a default generated double-extended-precision NaN.
| The `signExp' and `signif' values hold the most- and least-significant bits,
| respectively.
*----------------------------------------------------------------------------*/
#define floatx80_default_nan_high 0xFFFF
#define floatx80_default_nan_low  LIT64( 0xFFFFFFFFFFFFFFFF )

/*----------------------------------------------------------------------------
| Returns 1 if the double-extended-precision floating-point value `a' is a
| NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool floatx80_is_nan( floatx80 a )
{

    return ( ( a.signExp & 0x7FFF ) == 0x7FFF ) && (uint64_t) ( a.signif<<1 );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-extended-precision floating-point value `a' is a
| signaling NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool floatx80_is_signaling_nan( floatx80 a )
{
    uint64_t aLow;

    aLow = a.signif & ~ LIT64( 0x4000000000000000 );
    return
           ( ( a.signExp & 0x7FFF ) == 0x7FFF )
        && (uint64_t) ( aLow<<1 )
        && ( a.signif == aLow );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-extended-precision floating-
| point NaN `a' to the canonical NaN format.  If `a' is a signaling NaN, the
| invalid exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT floatx80ToCommonNaN( floatx80 a )
{
    commonNaNT z;

    if ( floatx80_is_signaling_nan( a ) ) float_raise( float_flag_invalid );
    z.sign = a.signExp>>15;
    z.signif = 0;
    z.signExp = a.signif<<1;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the double-
| extended-precision floating-point format.
*----------------------------------------------------------------------------*/

static floatx80 commonNaNToFloatx80( commonNaNT a )
{
    floatx80 z;

    z.signif = LIT64( 0xC000000000000000 ) | ( a.signExp>>1 );
    z.signExp = ( ( (uint16_t) a.sign )<<15 ) | 0x7FFF;
    return z;

}

/*----------------------------------------------------------------------------
| Takes two double-extended-precision floating-point values `a' and `b', one
| of which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static floatx80 propagateFloatx80NaN( floatx80 a, floatx80 b )
{
    bool aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    aIsNaN = floatx80_is_nan( a );
    aIsSignalingNaN = floatx80_is_signaling_nan( a );
    bIsNaN = floatx80_is_nan( b );
    bIsSignalingNaN = floatx80_is_signaling_nan( b );
    a.signif |= LIT64( 0xC000000000000000 );
    b.signif |= LIT64( 0xC000000000000000 );
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid );
    if ( aIsNaN ) {
        return ( aIsSignalingNaN & bIsNaN ) ? b : a;
    }
    else {
        return b;
    }

}

#endif

#ifdef FLOAT128

/*----------------------------------------------------------------------------
| The pattern for a default generated quadruple-precision NaN.  The `signExp' and
| `signif' values hold the most- and least-significant bits, respectively.
*----------------------------------------------------------------------------*/
#define float128_default_nan_high LIT64( 0xFFFFFFFFFFFFFFFF )
#define float128_default_nan_low  LIT64( 0xFFFFFFFFFFFFFFFF )

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is a NaN;
| otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float128_is_nan( float128 a )
{

    return
           ( LIT64( 0xFFFE000000000000 ) <= (uint64_t) ( a.signExp<<1 ) )
        && ( a.signif || ( a.signExp & LIT64( 0x0000FFFFFFFFFFFF ) ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is a
| signaling NaN; otherwise returns 0.
*----------------------------------------------------------------------------*/

bool float128_is_signaling_nan( float128 a )
{

    return
           ( ( ( a.signExp>>47 ) & 0xFFFF ) == 0xFFFE )
        && ( a.signif || ( a.signExp & LIT64( 0x00007FFFFFFFFFFF ) ) );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point NaN
| `a' to the canonical NaN format.  If `a' is a signaling NaN, the invalid
| exception is raised.
*----------------------------------------------------------------------------*/

static commonNaNT float128ToCommonNaN( float128 a )
{
    commonNaNT z;

    if ( float128_is_signaling_nan( a ) ) float_raise( float_flag_invalid );
    z.sign = a.signExp>>63;
    shortShift128Left( a.signExp, a.signif, 16, &z.signExp, &z.signif );
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the canonical NaN `a' to the quadruple-
| precision floating-point format.
*----------------------------------------------------------------------------*/

static float128 commonNaNToFloat128( commonNaNT a )
{
    float128 z;

    shift128Right( a.signExp, a.signif, 16, &z.signExp, &z.signif );
    z.signExp |= ( ( (uint64_t) a.sign )<<63 ) | LIT64( 0x7FFF800000000000 );
    return z;

}

/*----------------------------------------------------------------------------
| Takes two quadruple-precision floating-point values `a' and `b', one of
| which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

static float128 propagateFloat128NaN( float128 a, float128 b )
{
    bool aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    aIsNaN = float128_is_nan( a );
    aIsSignalingNaN = float128_is_signaling_nan( a );
    bIsNaN = float128_is_nan( b );
    bIsSignalingNaN = float128_is_signaling_nan( b );
    a.signExp |= LIT64( 0x0000800000000000 );
    b.signExp |= LIT64( 0x0000800000000000 );
    if ( aIsSignalingNaN | bIsSignalingNaN ) float_raise( float_flag_invalid );
    if ( aIsNaN ) {
        return ( aIsSignalingNaN & bIsNaN ) ? b : a;
    }
    else {
        return b;
    }

}

#endif

