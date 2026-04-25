"""Arbor noise — pure Python procedural noise for heightmap generation.

No UE5 or external dependencies. Can be tested in a standard Python shell.

Usage::

    from arbor.noise import generate_heightmap, heightmap_to_uint16

    hmap = generate_heightmap(505, 505, frequency=4.0, amplitude=0.5, seed=42)
    hmap_bytes = heightmap_to_uint16(hmap)
"""

import math
import struct
import random as _random


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _hash(ix, iy, seed=0):
    """Integer hash for 2D grid coordinates. Returns float in [0.0, 1.0]."""
    n = ix + iy * 57 + seed * 131
    n = (n << 13) ^ n
    n = (n * (n * n * 15731 + 789221) + 1376312589) & 0x7FFFFFFF
    return n / 0x7FFFFFFF


def _lerp(a, b, t):
    """Linear interpolation."""
    return a + (b - a) * t


def _smoothstep(t):
    """Hermite smoothstep: 3t^2 - 2t^3."""
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------------------
# Core noise functions
# ---------------------------------------------------------------------------

def value_noise_2d(x, y, seed=0):
    """Single-octave value noise at floating-point coordinates.

    Uses bilinear interpolation between hashed grid corners with
    smoothstep for C1 continuity.

    Returns:
        Float in [0.0, 1.0].
    """
    ix = int(math.floor(x))
    iy = int(math.floor(y))
    fx = x - ix
    fy = y - iy

    sx = _smoothstep(fx)
    sy = _smoothstep(fy)

    n00 = _hash(ix, iy, seed)
    n10 = _hash(ix + 1, iy, seed)
    n01 = _hash(ix, iy + 1, seed)
    n11 = _hash(ix + 1, iy + 1, seed)

    nx0 = _lerp(n00, n10, sx)
    nx1 = _lerp(n01, n11, sx)
    return _lerp(nx0, nx1, sy)


def fbm_2d(x, y, octaves=4, lacunarity=2.0, persistence=0.5, seed=0):
    """Fractal Brownian motion — sum of value noise octaves.

    Produces smooth, organic-looking noise suitable for rolling hills.

    Args:
        x, y: Coordinates (any float).
        octaves: Number of noise layers (1-8).
        lacunarity: Frequency multiplier per octave (typically 2.0).
        persistence: Amplitude decay per octave (typically 0.5).
        seed: Random seed for reproducibility.

    Returns:
        Float in approximately [0.0, 1.0] (normalized).
    """
    total = 0.0
    amplitude = 1.0
    frequency = 1.0
    max_value = 0.0

    for i in range(octaves):
        total += value_noise_2d(x * frequency, y * frequency, seed + i * 31) * amplitude
        max_value += amplitude
        amplitude *= persistence
        frequency *= lacunarity

    return total / max_value if max_value > 0 else 0.5


def ridge_noise_2d(x, y, octaves=4, lacunarity=2.0, persistence=0.5, seed=0):
    """Ridged multifractal noise for mountain ridges.

    Inverts the absolute value of noise to create sharp ridge lines,
    then layers octaves with successive squaring for more dramatic peaks.

    Returns:
        Float in approximately [0.0, 1.0].
    """
    total = 0.0
    amplitude = 1.0
    frequency = 1.0
    max_value = 0.0
    prev = 1.0

    for i in range(octaves):
        n = value_noise_2d(x * frequency, y * frequency, seed + i * 31)
        # Create ridges: 1.0 - abs(noise * 2 - 1)
        n = 1.0 - abs(n * 2.0 - 1.0)
        n = n * n  # sharpen ridges
        n *= prev  # weight by previous octave for detail in valleys
        prev = n
        total += n * amplitude
        max_value += amplitude
        amplitude *= persistence
        frequency *= lacunarity

    return total / max_value if max_value > 0 else 0.5


# ---------------------------------------------------------------------------
# Heightmap generation
# ---------------------------------------------------------------------------

def generate_heightmap(width, height, frequency=4.0, amplitude=1.0,
                       octaves=4, lacunarity=2.0, persistence=0.5,
                       seed=None, base_height=0.5, noise_type="fbm"):
    """Generate a 2D heightmap as a flat list of floats in [0.0, 1.0].

    Args:
        width: Heightmap width in pixels.
        height: Heightmap height in pixels.
        frequency: Base noise frequency (higher = more hills per area).
            2.0 = very gentle, 4.0 = moderate hills, 8.0 = many small hills.
        amplitude: Height variation multiplier (0.0-1.0).
            0.0 = flat, 0.5 = moderate hills, 1.0 = full range.
        octaves: Number of fBm layers (1-8). More = finer detail.
        lacunarity: Frequency multiplier per octave (typically 2.0).
        persistence: Amplitude decay per octave (typically 0.5).
        seed: Random seed. None = random.
        base_height: Center height in [0.0, 1.0]. 0.5 = middle.
        noise_type: ``"fbm"`` for rolling hills, ``"ridge"`` for mountains.

    Returns:
        ``list[float]`` of length ``width * height``, row-major,
        values clamped to [0.0, 1.0].
    """
    if seed is None:
        seed = _random.randint(0, 2**31 - 1)

    noise_fn = ridge_noise_2d if noise_type == "ridge" else fbm_2d

    data = []
    inv_w = frequency / max(width, 1)
    inv_h = frequency / max(height, 1)

    for row in range(height):
        ny = row * inv_h
        for col in range(width):
            nx = col * inv_w
            val = noise_fn(nx, ny, octaves=octaves, lacunarity=lacunarity,
                           persistence=persistence, seed=seed)
            val = base_height + (val - 0.5) * amplitude
            val = max(0.0, min(1.0, val))
            data.append(val)

    return data


def heightmap_to_uint16(heightmap, base=32768, scale=16384):
    """Convert float heightmap [0.0, 1.0] to uint16 bytearray for UE5.

    UE5 landscape heightmap format: uint16 little-endian packed values.
    ``32768`` = sea level / flat ground.

    Args:
        heightmap: ``list[float]`` from ``generate_heightmap()``.
        base: Center uint16 value (32768 for UE5 sea level).
        scale: Half-range in uint16 units. Default 16384 means
            heights span ``base +/- scale`` = [16384, 49152].

    Returns:
        ``bytearray`` of packed uint16 little-endian values,
        length = ``len(heightmap) * 2``.
    """
    buf = bytearray(len(heightmap) * 2)
    for i, val in enumerate(heightmap):
        # Map [0, 1] → [base - scale, base + scale]
        u16 = int(base + (val - 0.5) * 2.0 * scale)
        u16 = max(0, min(65535, u16))
        struct.pack_into('<H', buf, i * 2, u16)
    return buf


# ---------------------------------------------------------------------------
# River path generation
# ---------------------------------------------------------------------------

def generate_river_path(width, height, heightmap, num_points=10,
                        start_edge="north", seed=None, meander=0.3):
    """Generate a meandering river path across a heightmap.

    Uses gradient descent with noise perturbation to find a
    natural-looking river path that follows low terrain.

    Args:
        width, height: Heightmap dimensions (matching ``generate_heightmap``).
        heightmap: ``list[float]`` heightmap data.
        num_points: Number of spline control points to output.
        start_edge: Which edge to start from
            (``"north"``, ``"south"``, ``"east"``, ``"west"``).
        seed: Random seed.
        meander: Meander strength (0.0 = straight, 1.0 = very winding).

    Returns:
        List of ``(x_frac, y_frac)`` tuples in [0.0, 1.0] range,
        representing the river path as fractional coordinates
        across the heightmap.
    """
    if seed is None:
        seed = _random.randint(0, 2**31 - 1)

    rng = _random.Random(seed)

    def _sample(fx, fy):
        """Sample heightmap at fractional coords with bilinear interpolation."""
        px = fx * (width - 1)
        py = fy * (height - 1)
        ix = int(math.floor(px))
        iy = int(math.floor(py))
        fx2 = px - ix
        fy2 = py - iy
        ix = max(0, min(width - 2, ix))
        iy = max(0, min(height - 2, iy))
        n00 = heightmap[iy * width + ix]
        n10 = heightmap[iy * width + ix + 1]
        n01 = heightmap[(iy + 1) * width + ix]
        n11 = heightmap[(iy + 1) * width + ix + 1]
        return _lerp(_lerp(n00, n10, fx2), _lerp(n01, n11, fx2), fy2)

    # Determine start and end based on edge
    margin = 0.1  # stay away from exact edges
    if start_edge == "north":
        sx, sy = rng.uniform(0.3, 0.7), margin
        ex, ey = rng.uniform(0.3, 0.7), 1.0 - margin
    elif start_edge == "south":
        sx, sy = rng.uniform(0.3, 0.7), 1.0 - margin
        ex, ey = rng.uniform(0.3, 0.7), margin
    elif start_edge == "west":
        sx, sy = margin, rng.uniform(0.3, 0.7)
        ex, ey = 1.0 - margin, rng.uniform(0.3, 0.7)
    else:  # east
        sx, sy = 1.0 - margin, rng.uniform(0.3, 0.7)
        ex, ey = margin, rng.uniform(0.3, 0.7)

    # Walk from start to end, biasing toward lower terrain
    steps = max(num_points * 10, 50)
    path = []
    cx, cy = sx, sy

    for step in range(steps):
        t = step / max(steps - 1, 1)
        path.append((cx, cy))

        # Target direction (toward endpoint)
        target_x = _lerp(sx, ex, t + 1.0 / steps)
        target_y = _lerp(sy, ey, t + 1.0 / steps)
        dx = target_x - cx
        dy = target_y - cy

        # Sample gradient — bias toward lower terrain
        grad_step = 0.02
        if 0.01 < cx < 0.99 and 0.01 < cy < 0.99:
            h_left = _sample(cx - grad_step, cy)
            h_right = _sample(cx + grad_step, cy)
            h_up = _sample(cx, cy - grad_step)
            h_down = _sample(cx, cy + grad_step)
            # Gradient points uphill; we want to go downhill laterally
            gx = h_left - h_right  # positive = lower on left
            gy = h_up - h_down
            # Perpendicular to main direction for lateral displacement
            dx += gx * meander * 0.5
            dy += gy * meander * 0.5

        # Add noise-based meander
        noise_val = value_noise_2d(cx * 5.0, cy * 5.0, seed + 777) - 0.5
        perp_x = -dy
        perp_y = dx
        perp_len = math.sqrt(perp_x * perp_x + perp_y * perp_y)
        if perp_len > 1e-6:
            perp_x /= perp_len
            perp_y /= perp_len
        dx += perp_x * noise_val * meander * 0.3
        dy += perp_y * noise_val * meander * 0.3

        # Normalize step length
        step_len = 1.0 / steps
        d_len = math.sqrt(dx * dx + dy * dy)
        if d_len > 1e-6:
            cx += dx / d_len * step_len
            cy += dy / d_len * step_len
        else:
            cx += step_len * (1 if ex > sx else -1)

        # Clamp to valid range
        cx = max(0.02, min(0.98, cx))
        cy = max(0.02, min(0.98, cy))

    path.append((ex, ey))

    # Downsample to num_points
    if len(path) <= num_points:
        return path

    result = []
    for i in range(num_points):
        idx = int(i / max(num_points - 1, 1) * (len(path) - 1))
        result.append(path[idx])

    return result
