import random
import struct
from decimal import Decimal, getcontext, ROUND_DOWN, ROUND_UP

getcontext().prec = 900
random.seed(20261025)


def next_up(x):
    bits = struct.unpack("<Q", struct.pack("<d", x))[0]
    return struct.unpack("<d", struct.pack("<Q", bits + 1))[0]


def random_double():
    while True:
        kind = random.random()
        if kind < 0.5:
            bits = random.getrandbits(63)
            x = struct.unpack("<d", struct.pack("<Q", bits))[0]
        elif kind < 0.8:
            x = random.uniform(-1, 1) * 10 ** random.randint(-30, 30)
        else:
            x = float(random.getrandbits(random.randint(1, 64))) * 10 ** random.randint(-5, 5)
        x = abs(x)
        if x != 0 and x == x and x != float("inf") and next_up(x) != float("inf") and x >= 2.2250738585072014e-308:
            return x


def scientific(value, digits, rounding):
    exponent = value.adjusted()
    quantum = Decimal(1).scaleb(exponent - digits + 1)
    rounded = value.quantize(quantum, rounding=rounding)
    sign, digit_tuple, exp = rounded.as_tuple()
    text = "".join(str(d) for d in digit_tuple).lstrip("0") or "0"
    return "%se%d" % (text, exp)


lines = []
for _ in range(60000):
    x = random_double()
    midpoint = (Decimal(x) + Decimal(next_up(x))) / 2
    for digits in (17, 18, 19):
        lines.append(scientific(midpoint, digits, ROUND_DOWN))
        lines.append(scientific(midpoint, digits, ROUND_UP))
    exact = scientific(midpoint, 19, ROUND_DOWN)
    lines.append(exact)
    if random.random() < 0.5:
        lines.append(("-" if random.random() < 0.5 else "") + format(midpoint.normalize(), "f")[:40])

with open("halfway.txt", "w") as file:
    file.write("\n".join(lines) + "\n")
print(len(lines), "halfway strings")
