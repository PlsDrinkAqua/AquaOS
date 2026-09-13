/* Adapted from musl src/stdio/vfprintf.c, retrieved 2026-09-13.
 * Copyright 2005-2020 Rich Felker et al.; MIT license in licenses/musl-COPYRIGHT.
 * Adaptation: ZenOS output sink and helpers; numeric algorithm retained. */
#include <float.h>
#include <math.h>
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define LEFT_ADJ 1
#define MARK_POS 2
#define PAD_POS 4
#define ALT_FORM 8
#define ZERO_PAD 16
#define BIGLPRE 1
static const char xdigits[] = "0123456789ABCDEF";
static char *float_uint(unsigned value, char *end) {
    while (value) { *--end = '0' + value % 10; value /= 10; }
    return end;
}
static void float_pad(output_t *f, char c, int width, int length, int flags) {
    if (!(flags & (LEFT_ADJ | ZERO_PAD)) && width > length) pad(f, c, (size_t)(width - length));
}
static int fmt_fp(output_t *f, long double y, int w, int p, int fl, int t, int ps)
{
	int max_mant_dig = (ps==BIGLPRE) ? LDBL_MANT_DIG : DBL_MANT_DIG;
	int max_exp = (ps==BIGLPRE) ? LDBL_MAX_EXP : DBL_MAX_EXP;
	/* One slot for 29 bits left of radix point, a slot for every 29-21=8
	 * bits right of the radix point, and one final zero slot. */
	int max_mant_slots = 1 + (max_mant_dig-29+7)/8 + 1;
	int max_exp_slots = (max_exp+max_mant_dig+28+8)/9;
	int bufsize = max_mant_slots + max_exp_slots;
	uint32_t big[bufsize];
	uint32_t *a, *d, *r, *z;
	int e2=0, e, i, j, l;
	char buf[9+LDBL_MANT_DIG/4], *s;
	const char *prefix="-0X+0X 0X-0x+0x 0x";
	int pl;
	char ebuf0[3*sizeof(int)], *ebuf=&ebuf0[3*sizeof(int)], *estr;

	pl=1;
	if (signbit(y)) {
		y=-y;
	} else if (fl & MARK_POS) {
		prefix+=3;
	} else if (fl & PAD_POS) {
		prefix+=6;
	} else prefix++, pl=0;

	if (!isfinite(y)) {
		char *s = (t&32)?"inf":"INF";
		if (y!=y) s=(t&32)?"nan":"NAN";
		float_pad(f, ' ', w, 3+pl, fl&~ZERO_PAD);
		emit(f, prefix, pl);
		emit(f, s, 3);
		float_pad(f, ' ', w, 3+pl, fl^LEFT_ADJ);
		return MAX(w, 3+pl);
	}

	y = frexpl(y, &e2) * 2;
	if (y) e2--;

	if ((t|32)=='a') {
		if (t&32) prefix += 9;
		pl += 2;

		if (p>=0 && p<(LDBL_MANT_DIG-1+3)/4) {
			double round = scalbn(1, LDBL_MANT_DIG-1-(p*4));
			if (*prefix=='-') {
				y=-y;
				y-=round;
				y+=round;
				y=-y;
			} else {
				y+=round;
				y-=round;
			}
		}

		estr=float_uint(e2<0 ? -e2 : e2, ebuf);
		if (estr==ebuf) *--estr='0';
		*--estr = (e2<0 ? '-' : '+');
		*--estr = t+('p'-'a');

		s=buf;
		do {
			int x=y;
			*s++=xdigits[x]|(t&32);
			y=16*(y-x);
			if (s-buf==1 && (y||p>0||(fl&ALT_FORM))) *s++='.';
		} while (y);

		if (p > INT_MAX-2-(ebuf-estr)-pl)
			return -1;
		if (p && s-buf-2 < p)
			l = (p+2) + (ebuf-estr);
		else
			l = (s-buf) + (ebuf-estr);

		float_pad(f, ' ', w, pl+l, fl);
		emit(f, prefix, pl);
		float_pad(f, '0', w, pl+l, fl^ZERO_PAD);
		emit(f, buf, s-buf);
		float_pad(f, '0', l-(ebuf-estr)-(s-buf), 0, 0);
		emit(f, estr, ebuf-estr);
		float_pad(f, ' ', w, pl+l, fl^LEFT_ADJ);
		return MAX(w, pl+l);
	}
	if (p<0) p=6;

	if (y) y *= 0x1p28, e2-=28;

	if (e2<0) a=r=z=big;
	else a=r=z=big+sizeof(big)/sizeof(*big) - max_mant_slots - 1;

	do {
		*z = y;
		y = 1000000000*(y-*z++);
	} while (y);

	while (e2>0) {
		uint32_t carry=0;
		int sh=MIN(29,e2);
		for (d=z-1; d>=a; d--) {
			uint64_t x = ((uint64_t)*d<<sh)+carry;
			*d = x % 1000000000;
			carry = x / 1000000000;
		}
		if (carry) *--a = carry;
		while (z>a && !z[-1]) z--;
		e2-=sh;
	}
	while (e2<0) {
		uint32_t carry=0, *b;
		int sh=MIN(9,-e2), need=1+(p+LDBL_MANT_DIG/3U+8)/9;
		for (d=a; d<z; d++) {
			uint32_t rm = *d & ((1<<sh)-1);
			*d = (*d>>sh) + carry;
			carry = (1000000000>>sh) * rm;
		}
		if (!*a) a++;
		if (carry) *z++ = carry;
		/* Avoid (slow!) computation past requested precision */
		b = (t|32)=='f' ? r : a;
		if (z-b > need) z = b+need;
		e2+=sh;
	}

	if (a<z) for (i=10, e=9*(r-a); *a>=(uint32_t)i; i*=10, e++);
	else e=0;

	/* Perform rounding: j is precision after the radix (possibly neg) */
	j = p - ((t|32)!='f')*e - ((t|32)=='g' && p);
	if (j < 9*(z-r-1)) {
		uint32_t x;
		/* We avoid C's broken division of negative numbers */
		d = r + 1 + ((j+9*LDBL_MAX_EXP)/9 - LDBL_MAX_EXP);
		j += 9*LDBL_MAX_EXP;
		j %= 9;
		for (i=10, j++; j<9; i*=10, j++);
		x = *d % i;
		/* Are there any significant digits past j? */
		if (x || d+1!=z) {
			long double round = 2/LDBL_EPSILON;
			long double small;
			if ((*d/i & 1) || (i==1000000000 && d>a && (d[-1]&1)))
				round += 2;
			if (x<(uint32_t)i/2) small=0x0.8p0;
			else if (x==(uint32_t)i/2 && d+1==z) small=0x1.0p0;
			else small=0x1.8p0;
			if (pl && *prefix=='-') round*=-1, small*=-1;
			*d -= x;
			/* Decide whether to round by probing round+small */
			if (round+small != round) {
				*d = *d + i;
				while (*d > 999999999) {
					*d--=0;
					if (d<a) *--a=0;
					(*d)++;
				}
				for (i=10, e=9*(r-a); *a>=(uint32_t)i; i*=10, e++);
			}
		}
		if (z>d+1) z=d+1;
	}
	for (; z>a && !z[-1]; z--);
	
	if ((t|32)=='g') {
		if (!p) p++;
		if (p>e && e>=-4) {
			t--;
			p-=e+1;
		} else {
			t-=2;
			p--;
		}
		if (!(fl&ALT_FORM)) {
			/* Count trailing zeros in last place */
			if (z>a && z[-1]) for (i=10, j=0; z[-1]%i==0; i*=10, j++);
			else j=9;
			if ((t|32)=='f')
				p = MIN(p,MAX(0,9*(z-r-1)-j));
			else
				p = MIN(p,MAX(0,9*(z-r-1)+e-j));
		}
	}
	if (p > INT_MAX-1-(p || (fl&ALT_FORM)))
		return -1;
	l = 1 + p + (p || (fl&ALT_FORM));
	if ((t|32)=='f') {
		if (e > INT_MAX-l) return -1;
		if (e>0) l+=e;
	} else {
		estr=float_uint(e<0 ? -e : e, ebuf);
		while(ebuf-estr<2) *--estr='0';
		*--estr = (e<0 ? '-' : '+');
		*--estr = t;
		if (ebuf-estr > INT_MAX-l) return -1;
		l += ebuf-estr;
	}

	if (l > INT_MAX-pl) return -1;
	float_pad(f, ' ', w, pl+l, fl);
	emit(f, prefix, pl);
	float_pad(f, '0', w, pl+l, fl^ZERO_PAD);

	if ((t|32)=='f') {
		if (a>r) a=r;
		for (d=a; d<=r; d++) {
			char *s = float_uint(*d, buf+9);
			if (d!=a) while (s>buf) *--s='0';
			else if (s==buf+9) *--s='0';
			emit(f, s, buf+9-s);
		}
		if (p || (fl&ALT_FORM)) emit(f, ".", 1);
		for (; d<z && p>0; d++, p-=9) {
			char *s = float_uint(*d, buf+9);
			while (s>buf) *--s='0';
			emit(f, s, MIN(9,p));
		}
		float_pad(f, '0', p+9, 9, 0);
	} else {
		if (z<=a) z=a+1;
		for (d=a; d<z && p>=0; d++) {
			char *s = float_uint(*d, buf+9);
			if (s==buf+9) *--s='0';
			if (d!=a) while (s>buf) *--s='0';
			else {
				emit(f, s++, 1);
				if (p>0||(fl&ALT_FORM)) emit(f, ".", 1);
			}
			emit(f, s, MIN(buf+9-s, p));
			p -= buf+9-s;
		}
		float_pad(f, '0', p+18, 18, 0);
		emit(f, estr, ebuf-estr);
	}

	float_pad(f, ' ', w, pl+l, fl^LEFT_ADJ);

	return MAX(w, pl+l);
}

