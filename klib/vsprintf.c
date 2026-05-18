#include <klib/stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define APPEND_CHAR(c) do { \
	if (str && written < size - 1) { \
		str[written] = (c);      \
	}                                \
	written++;                       \
} while (0)

static const char *lower_digits = "0123456789abcdef";
static const char *upper_digits = "0123456789ABCDEF";

static int print_number(char **str_ptr, size_t *size_ptr, size_t written, 
                        uint64_t num, int base, bool is_signed, 
                        int pad_width, char pad_char, bool use_upper) {
    char buf[64];
    int pos = 0;
    bool is_negative = false;
    const char *digits = use_upper ? upper_digits : lower_digits;

    char *str = *str_ptr;
    size_t size = *size_ptr;
    size_t start_written = written;

    /* sign */
    if (is_signed && (int64_t)num < 0) {
        is_negative = true;
        num = (uint64_t)(-(int64_t)num);
    }

    /* digit to string */
    do {
        buf[pos++] = digits[num % base];
        num /= base;
    } while (num > 0);

    /* sign '-' takes up 1 pad_width */
    if (is_negative) {
        pad_width--;
    }

    /* pad leading characters */
    while (pos < pad_width) {
        APPEND_CHAR(pad_char);
        pad_width--;
    }

    /* the '-' */
    if (is_negative) {
        APPEND_CHAR('-');
    }

    /* output numbers in reverse order */
    while (pos > 0) {
        APPEND_CHAR(buf[--pos]);
    }

    *str_ptr = str;
    *size_ptr = size;
    return written - start_written;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
	size_t written = 0;

	while (*format) {
		if (*format != '%') {
			APPEND_CHAR(*format++);
			continue;
		}

		format++; /* jump over '%' */

		/* parse pad char */
		char pad_char = ' ';
		int pad_width = 0;
		if (*format == '0') {
			pad_char = '0';
			format++;
		}
		
		/* parse fill width */
		while (*format >= '0' && *format <= '9') {
			pad_width = pad_width * 10 + (*format - '0');
			format++;
		}

		/* parse 'l'/'ll' */
		int is_long = 0;
		while (*format == 'l') {
			is_long++;
			format++;
		}

		/* parse specifier */
		char specifier = *format++;
		switch (specifier) {
		case 'c': {
			char c = (char)va_arg(ap, int);
			APPEND_CHAR(c);
			break;
		}
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s) s = "(null)";
			while (*s) {
				APPEND_CHAR(*s++);
			}
			break;
		}
		case 'd':
		case 'i': {
			int64_t val = (is_long >= 2) ? va_arg(ap, int64_t) : 
				(is_long == 1) ? va_arg(ap, long) : va_arg(ap, int);
			written += print_number(&str, &size, written, (uint64_t)val, 10, true, pad_width, pad_char, false);
			break;
		}
		case 'u': {
			uint64_t val = (is_long >= 2) ? va_arg(ap, uint64_t) : 
				(is_long == 1) ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
			written += print_number(&str, &size, written, val, 10, false, pad_width, pad_char, false);
			break;
		}
		case 'x':
		case 'X': {
			uint64_t val = (is_long >= 2) ? va_arg(ap, uint64_t) : 
				(is_long == 1) ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
			written += print_number(&str, &size, written, val, 16, false, pad_width, pad_char, (specifier == 'X'));
			break;
		}
		case 'p': {
			uintptr_t val = va_arg(ap, uintptr_t);
			APPEND_CHAR('0');
			APPEND_CHAR('x');
			written += print_number(&str, &size, written, (uint64_t)val, 16, false, sizeof(uintptr_t) * 2, '0', false);
			break;
		}
		case '%': {
			APPEND_CHAR('%');
			break;
		}
		default: {
			/* unsupported character */
			APPEND_CHAR('%');
			APPEND_CHAR(specifier);
			break;
		}
		}
	}

	/* append terminal */
	if (size > 0) {
		if (written < size) {
			str[written] = '\0';
		} else {
			str[size - 1] = '\0';
		}
	}

	return written;
}

int snprintf(char *str, size_t size, const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	int ret = vsnprintf(str, size, format, ap);
	va_end(ap);
	return ret;
}