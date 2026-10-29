"""Shared verification primitive; production sampling remains in C++."""


class BlockSampler:
    """Independent MT19937-64 and explicit non-circular block sampling."""

    mask = (1 << 64) - 1

    def __init__(self, size, block, seed):
        self.size, self.block = size, block
        self.state = [seed & self.mask]
        for i in range(1, 312):
            x = self.state[-1]
            self.state.append((6364136223846793005 * (x ^ (x >> 62)) + i) & self.mask)
        self.position = 312

    def integer(self):
        if self.position == 312:
            for i in range(312):
                x = (self.state[i] & 0xFFFFFFFF80000000) | (
                    self.state[(i + 1) % 312] & 0x7FFFFFFF
                )
                self.state[i] = (
                    self.state[(i + 156) % 312]
                    ^ (x >> 1)
                    ^ (0xB5026F5AA96619E9 if x & 1 else 0)
                )
            self.position = 0
        x = self.state[self.position]
        self.position += 1
        x ^= (x >> 29) & 0x5555555555555555
        x ^= (x << 17) & 0x71D67FFFEDA60000
        x ^= (x << 37) & 0xFFF7EEE000000000
        return (x ^ (x >> 43)) & self.mask

    def blocks(self):
        bound = self.size - self.block + 1
        if not 0 < self.block <= self.size:
            raise ValueError("Invalid block calendar")
        threshold = ((1 << 64) - bound) % bound
        count = 0
        result = []
        while count < self.size:
            value = self.integer()
            while value < threshold:
                value = self.integer()
            length = min(self.block, self.size - count)
            result.append((value % bound, length))
            count += length
        return result
