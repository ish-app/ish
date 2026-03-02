//
//  ExceptionExfiltrator.m
//  libiSHApp
//
//  Created by Saagar Jha on 5/5/23.
//

#import "ExceptionExfiltrator.h"
#import <Foundation/Foundation.h>
#import <execinfo.h>
#import <pthread.h>

#define f(name, character)         \
	__asm__("\" " name "\": nop"); \
	void ish_exception_exfiltrate_##character(void) __asm__(" " name)

f("unprintable", unprintable);

f(" ", space);
f("!", exclamation_mark);
f("quotation_mark", quotation_mark);
f("#", number_sign);
f("$", dollar_sign);
f("%", percent_sign);
f("&", ampersand);
f("'", apostrophe);
f("(", left_parenthesis);
f(")", right_parenthesis);
f("*", asterisk);
f("+", plus_sign);
f(",", comma);
f("-", hyphen_minus);
f(".", full_stop);
f("/", solidus);
f("0", 0);
f("1", 1);
f("2", 2);
f("3", 3);
f("4", 4);
f("5", 5);
f("6", 6);
f("7", 7);
f("8", 8);
f("9", 9);
f(":", colon);
f(";", semicolon);
f("<", less_than_sign);
f("=", equals_sign);
f(">", greater_than_sign);
f("?", question_mark);
f("@", commercial_at);
f("A", A);
f("B", B);
f("C", C);
f("D", D);
f("E", E);
f("F", F);
f("G", G);
f("H", H);
f("I", I);
f("J", J);
f("K", K);
f("L", L);
f("M", M);
f("N", N);
f("O", O);
f("P", P);
f("Q", Q);
f("R", R);
f("S", S);
f("T", T);
f("U", U);
f("V", V);
f("W", W);
f("X", X);
f("Y", Y);
f("Z", Z);
f("[", left_square_bracket);
f("reverse_solidus", reverse_solidus);
f("]", right_square_bracket);
f("^", circumflex_accent);
f("_", low_line);
f("`", grave_accent);
f("a", a);
f("b", b);
f("c", c);
f("d", d);
f("e", e);
f("f", f);
f("g", g);
f("h", h);
f("i", i);
f("j", j);
f("k", k);
f("l", l);
f("m", m);
f("n", n);
f("o", o);
f("p", p);
f("q", q);
f("r", r);
f("s", s);
f("t", t);
f("u", u);
f("v", v);
f("w", w);
f("x", x);
f("y", y);
f("z", z);
f("{", left_curly_bracket);
f("|", vertical_line);
f("}", right_curly_bracket);
f("~", tilde);

#undef f

#define f(character, name) [character] = ish_exception_exfiltrate_##name

static void (*character2function[256])(void) = {
    f(' ', space),
    f('!', exclamation_mark),
    f('"', quotation_mark),
    f('#', number_sign),
    f('$', dollar_sign),
    f('%', percent_sign),
    f('&', ampersand),
    f('\'', apostrophe),
    f('(', left_parenthesis),
    f(')', right_parenthesis),
    f('*', asterisk),
    f('+', plus_sign),
    f(',', comma),
    f('-', hyphen_minus),
    f('.', full_stop),
    f('/', solidus),
    f('0', 0),
    f('1', 1),
    f('2', 2),
    f('3', 3),
    f('4', 4),
    f('5', 5),
    f('6', 6),
    f('7', 7),
    f('8', 8),
    f('9', 9),
    f(':', colon),
    f(';', semicolon),
    f('<', less_than_sign),
    f('=', equals_sign),
    f('>', greater_than_sign),
    f('?', question_mark),
    f('@', commercial_at),
    f('A', A),
    f('B', B),
    f('C', C),
    f('D', D),
    f('E', E),
    f('F', F),
    f('G', G),
    f('H', H),
    f('I', I),
    f('J', J),
    f('K', K),
    f('L', L),
    f('M', M),
    f('N', N),
    f('O', O),
    f('P', P),
    f('Q', Q),
    f('R', R),
    f('S', S),
    f('T', T),
    f('U', U),
    f('V', V),
    f('W', W),
    f('X', X),
    f('Y', Y),
    f('Z', Z),
    f('[', left_square_bracket),
    f('\\', reverse_solidus),
    f(']', right_square_bracket),
    f('^', circumflex_accent),
    f('_', low_line),
    f('`', grave_accent),
    f('a', a),
    f('b', b),
    f('c', c),
    f('d', d),
    f('e', e),
    f('f', f),
    f('g', g),
    f('h', h),
    f('i', i),
    f('j', j),
    f('k', k),
    f('l', l),
    f('m', m),
    f('n', n),
    f('o', o),
    f('p', p),
    f('q', q),
    f('r', r),
    f('s', s),
    f('t', t),
    f('u', u),
    f('v', v),
    f('w', w),
    f('x', x),
    f('y', y),
    f('z', z),
    f('{', left_curly_bracket),
    f('|', vertical_line),
    f('}', right_curly_bracket),
    f('~', tilde),
};

#undef f

void __ish_exception_exfiltrate_NAME__(void) {
	__asm__("nop");
}

void __ish_exception_exfiltrate_REASON__(void) {
	__asm__("nop");
}

void __ish_exception_exfiltrate_BACKTRACE__(void) {
	__asm__("nop");
}

static void (*function_for_character(unichar character))(void) {
	return character < sizeof(character2function) / sizeof(*character2function) ? (character2function[character] ? character2function[character] : ish_exception_exfiltrate_unprintable) : ish_exception_exfiltrate_unprintable;
}

static void *address_for_function(void (*function)(void)) {
	return (void *)((uintptr_t)function + 1);
}

struct frame {
	void *next;
	void *address;
};

static void *__ish_exception_exfiltrate_THREAD__(void *frames) {
	*(void **)__builtin_frame_address(0) = frames;
	__builtin_trap();
}

void iSHExceptionHandler(NSException *exception) {
	NSArray<NSNumber *> *backtrace = exception.callStackReturnAddresses;
	NSString *name = exception.name;
	NSString *reason = exception.reason;
	size_t size =
	    backtrace.count + /* backtrace frames */
	    1 +               /* __ish_exception_exfiltrate_BACKTRACE__ */
	    name.length +     /* name */
	    1 +               /* __ish_exception_exfiltrate__NAME__ */
	    reason.length +   /* reason */
	    1;                /* __ish_exception_exfiltrate__REASON__ */
	struct frame *frames = malloc(sizeof(struct frame) * size);
	frames[0].next = NULL;

	for (size_t i = 1; i < size; ++i) {
		frames[i].next = frames + i - 1;
	}

	size_t index = 0;

	for (NSNumber *address in backtrace.reverseObjectEnumerator) {
		frames[index++].address = address.pointerValue;
	}

	frames[index++].address = address_for_function(__ish_exception_exfiltrate_BACKTRACE__);

	for (NSUInteger i = 0; i < reason.length; ++i, ++index) {
		frames[index].address = address_for_function(function_for_character([reason characterAtIndex:reason.length - i - 1]));
	}

	frames[index++].address = address_for_function(__ish_exception_exfiltrate_REASON__);

	for (NSUInteger i = 0; i < name.length; ++i, ++index) {
		frames[index].address = address_for_function(function_for_character([name characterAtIndex:name.length - i - 1]));
	}

	frames[index++].address = address_for_function(__ish_exception_exfiltrate_NAME__);

	pthread_t thread;
	pthread_create(&thread, NULL, __ish_exception_exfiltrate_THREAD__, frames + size - 1);
	pthread_join(thread, NULL);
}
