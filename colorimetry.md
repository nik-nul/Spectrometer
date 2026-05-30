# Colorimetry (CCT/CRI)

This document summarizes the actual computation used by the current
implementation in `lib/colorimetry.c`.

## Inputs and resampling

- **Live SPD**: `spec_array_irradiance[]` is sampled onto 380-780 nm at
  1 nm steps using linear interpolation from pixel bins. Values outside
  `[nm_start, nm_end]` are set to 0.
- **CSV SPD**: reads `Nanometers,Intensity(a.u.)` from CSV and samples by
  linear interpolation. Values outside the CSV wavelength range are set to 0.

All calculations use the same 1 nm grid from 380 to 780 nm (inclusive).

## Reference tables

- CIE 1931 2-deg CMFs: `ref/CIE_xyz_1931_2deg.csv`
- CRI R1-R15 reflectance tables: `ref/CIE_srf_cri.csv` (resampled to 1 nm)
- CIE daylight basis S0/S1/S2: `ref/S.csv` (resampled to 1 nm)

## XYZ, xy, and 1960 UCS

Given SPD $S(\lambda)$ and CMFs $\bar{x}, \bar{y}, \bar{z}$:

$$X = K \sum S(\lambda)\bar{x}(\lambda),\quad
Y = K \sum S(\lambda)\bar{y}(\lambda),\quad
Z = K \sum S(\lambda)\bar{z}(\lambda)$$

$$K = 100 / \sum S(\lambda)\bar{y}(\lambda)$$

Chromaticity:

$$x = \frac{X}{X + Y + Z},\quad y = \frac{Y}{X + Y + Z}$$

CIE 1960 UCS:

$$u = \frac{4X}{X + 15Y + 3Z},\quad v = \frac{6Y}{X + 15Y + 3Z}$$

## CCT estimation (McCamy + UCS distance search)

Initial estimate (McCamy):

$$n = \frac{x - 0.3320}{0.1858 - y}$$
$$\text{CCT} = 449n^3 + 3525n^2 + 6823.3n + 5520.33$$

Refinement uses the minimum distance in 1960 UCS to the **blackbody locus**:

$$d = \sqrt{(u - u_T)^2 + (v - v_T)^2}$$

- Search range: $1000\text{ K}$ to $25000\text{ K}$
- Coarse sweep: 100 K steps in $[0.5\,\text{CCT}, 1.5\,\text{CCT}]$
- Refinement: 10 K then 1 K steps around the best result

Note: the distance search uses blackbody SPDs for all $T$, even when
$\text{CCT} \ge 5000\text{ K}$.

## Reference illuminant SPD

### Blackbody (for CCT < 5000 K)

Planck law (relative SPD, no explicit scaling):

$$S_B(\lambda, T) = \frac{1}{\lambda^5\left[\exp\left(\frac{c_2}{\lambda T}\right)-1\right]}$$

with $c_2 = 1.4388 \times 10^{-2}\,\text{m\,K}$ and $\lambda$ in meters.
The effective $T$ uses a small scale correction:

$$T = \text{CCT} \times 1.000556$$

### CIE daylight (for CCT >= 5000 K)

Compute $x_D$ from $T$:

$$x_D = \begin{cases}
-4.6070\times 10^9 / T^3 + 2.9678\times 10^6 / T^2 + 0.09911\times 10^3 / T + 0.244063 & 4000 \le T \le 7000\\
-2.0064\times 10^9 / T^3 + 1.9018\times 10^6 / T^2 + 0.24748\times 10^3 / T + 0.237040 & 7000 < T \le 25000
\end{cases}$$

$$y_D = -3.0x_D^2 + 2.870x_D - 0.275$$

$$M = 0.0241 + 0.2562x_D - 0.7341y_D$$
$$M_1 = (-1.3515 - 1.7703x_D + 5.9114y_D) / M$$
$$M_2 = (0.0300 - 31.4424x_D + 30.0717y_D) / M$$

$$S_D(\lambda) = S_0(\lambda) + M_1 S_1(\lambda) + M_2 S_2(\lambda)$$

## CRI calculation (R1-R15, Ra)

For each CRI sample reflectance $\rho_i(\lambda)$:

1) Compute XYZ under **test** SPD using the same $K$ as the test illuminant.
2) Compute XYZ under **reference** SPD using the reference $K$.
3) Convert both to UCS $(u,v)$.
4) Apply von Kries adaptation:

$$c = \frac{4 - u - 10v}{v},\quad d = \frac{1.708v + 0.404 - 1.481u}{v}$$

$$c'_k = c_n\frac{c_{k1}}{c_k},\quad d'_k = d_n\frac{d_{k1}}{d_k}$$

$$u' = \frac{10.872 + 0.404c' - 4d'}{16.518 + 1.481c' - d'},\quad
v' = \frac{5.520}{16.518 + 1.481c' - d'}$$

5) Convert to CIE 1964 $W^\*U^\*V^\*$:

$$W^\* = 25Y^{1/3} - 17$$
$$U^\* = 13W^\*(u - u_n),\quad V^\* = 13W^\*(v - v_n)$$

6) Color difference and special CRI:

$$\Delta E = \sqrt{(W^\*_k - W^\*_n)^2 + (U^\*_k - U^\*_n)^2 + (V\^*_k - V^\*_n)^2}$$

$$R_i = 100 - 4.6\Delta E$$

7) General CRI:

$$R_a = \frac{1}{8}\sum_{i=1}^{8} R_i$$

## Output

- **CCT** and **Ra** are shown on the SDL overlay by default.
- **R1-R15** and **Ra** can be printed on demand (press `c`) or from CSV
  via `-R <file>`.
