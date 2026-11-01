import sys
from fractions import Fraction

minimum, maximum = -342, 326


def floor_log2_pow10(q):
    return (q * 217706) >> 16


def floor_log10_pow2(n):
    return (n * 1262611) >> 22


def floor_log10_three_quarters_pow2(n):
    return (n * 1262611 - 524072) >> 22


def exact_floor_log2_pow10(q):
    value = Fraction(10) ** q
    e = 0
    while Fraction(2) ** (e + 1) <= value:
        e += 1
    while Fraction(2) ** e > value:
        e -= 1
    return e


def exact_floor_log10_pow2(n):
    value = Fraction(2) ** n
    e = 0
    while Fraction(10) ** (e + 1) <= value:
        e += 1
    while Fraction(10) ** e > value:
        e -= 1
    return e


for q in range(minimum, maximum + 1):
    assert floor_log2_pow10(q) == exact_floor_log2_pow10(q), q
for n in range(-1100, 1100):
    assert floor_log10_pow2(n) == exact_floor_log10_pow2(n), n
for n in range(-1100, 1100):
    value = Fraction(3, 4) * Fraction(2) ** n
    e = exact_floor_log10_pow2(n) - 1
    while Fraction(10) ** (e + 1) <= value:
        e += 1
    assert floor_log10_three_quarters_pow2(n) == e, n
print("formulas ok", file=sys.stderr)


def shift_for(q):
    """Returns the power of two that scales 5^q into [2^127, 2^128)."""
    return 127 - floor_log2_pow10(q) + q


def entry(q):
    """Returns 5^q scaled to 128 bits and truncated. For -27 <= q < 0 it is then rounded up, because in that range the
    reader needs the upper bound."""
    scaled = Fraction(5) ** q * Fraction(2) ** shift_for(q)
    value = scaled.numerator // scaled.denominator
    return value + 1 if -27 <= q < 0 else value


def entry_by_integers(q):
    """Computes the same entry with integer shifts and division alone, so that it serves as an independent check."""
    shift = shift_for(q)
    if q >= 0:
        value = 5 ** q << shift if shift >= 0 else 5 ** q >> -shift
    else:
        value = (1 << shift) // 5 ** -q
    return value + 1 if -27 <= q < 0 else value


entries = [(q, entry(q)) for q in range(minimum, maximum + 1)]
for q, t in entries:
    assert t == entry_by_integers(q), q

worst = Fraction(0)
for q, t in entries:
    assert (1 << 127) <= t < (1 << 128), q
    normalized = Fraction(5) ** q * Fraction(2) ** shift_for(q)
    assert (normalized.denominator == 1) == (0 <= q <= 55), q
    s = floor_log2_pow10(q) - q - 127
    exact = Fraction(5) ** q
    approx = Fraction(t) * Fraction(2) ** s
    error = abs(approx / exact - 1)
    worst = max(worst, error)
bits = 0
while Fraction(1, 2 ** (bits + 1)) >= worst:
    bits += 1
print("worst relative error below 2^-%d" % bits, file=sys.stderr)

table = dict(entries)
for fraction_bits, bias in ((23, 127), (52, 1023)):
    fives = 0
    while 5 ** (fives + 1) < 2 ** (fraction_bits + 2):
        fives += 1
    for biased in range(0, 2 * bias + 1):
        e = max(biased, 1) - bias - fraction_bits
        k = 2 - floor_log10_pow2(e)
        power = table[k] + (1 if k < -27 or k > 55 else 0)
        delta = (power >> 64) >> (63 - e - floor_log2_pow10(k))
        thousands = e - 1 + k >= 3 and k + fives >= 3
        assert 100 <= delta < 1000 and (delta >= 102 or not thousands), (fraction_bits, e)
print("last digit ok", file=sys.stderr)

lines = []
for q, t in entries:
    lines.append("0x%016XULL, 0x%016XULL," % (t >> 64, t & ((1 << 64) - 1)))
sys.stdout.write("\n".join(lines) + "\n")
