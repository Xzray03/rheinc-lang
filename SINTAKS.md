# SINTAKS.md — Sintaks Bahasa Rheinc

Dokumen ini merangkum sintaks Rheinc sebagaimana dipraktikkan di korpus
`Sintaks-awal/`: 81 file `.rhc` + 5 header `.rhh` di subfolder `rhpp/`.
Setiap konstruksi diturunkan langsung dari contoh nyata di korpus.
Rujukan penyimpangan semantik: `DEVIATIONS.md`.

## 1. Struktur Program & Komentar

```rheinc
prosedur utama() {                 -- entry point (fungsi/prosedur bernama `utama`)
    tampilkan "Halo, Dunia!\n"
}
```

- File `.rhc` (executable) wajib punya **tepat 1** entry; 0 atau >1 -> error.
- Entry boleh `prosedur` (void) atau `fungsi ... -> T` — nilai kembalian
  `utama` menjadi **exit code** program (`kembalikan 0` = sukses).
- File `.rhh` (declaration, tanpa entry) dimuat via preprocessor.
- Komentar baris: `--` sampai akhir baris (juga bisa di akhir baris kode).
- Komentar blok: `--[ ... ]--` — multi-baris **dan** bisa sisip di
  tengah ekspresi: `variabel x: bilangan = --[ sisipan ]-- 5`.

## 2. Token & Literal

| Literal | Contoh |
|---|---|
| Integer desimal | `42`, `-9223372036854775808` |
| Integer heksadesimal | `0xFF`, `0X0f`, `0xDEAD_BEEF` |
| Integer biner | `0b0`, `0b1010`, `0B1111_0000` |
| Pemisah digit `_` | `1_000_000`, `3.141_592_653`, `1_000.5e1_0` |
| Desimal (double) | `3.14`, `-0.0`, `1e-10` |
| Karakter | `'A'`, `'\n'`, `'\r'`, `'\t'`, `'\\'`, `'\''`, `'\"'`, `'\0'`, `'あ'` (unicode) |
| Teks (escape) | `"...\n\t\"quote\"..."`, unicode langsung: `"こんにちは世界 🌍"` |
| Teks mentah | `Raw("C:\temp\baru\n")` — tanpa escape, khusus pola/path |
| Boolean | `benar`, `salah` |
| Larik | `[10, 20, 30]`, `[1, 2, 3, 4, 5, 6, 7, 8, 9]` |
| Pointer null | `kosong` |

Komentar blok `--[ ... ]--` juga merupakan komentar lexer ("tidak di-lex").
Error lexical (literal tak tertutup, escape salah, `0x`/`0b` kosong, `_`
ganda, `1__000`, `0x_FF`) dilaporkan dengan pemulihan (lexer tetap lanjut).

## 3. Tipe Data

### 3.1 Primitif

| Tipe | Keterangan |
|---|---|
| `bilangan` | integer 64-bit signed (default) |
| `bilangan8` / `bilangan16` / `bilangan32` / `bilangan64` | fixed-width signed |
| `bilangan_tanpa_tanda8/16/32/64` | unsigned fixed-width (`255` .. `18446744073709551615`) |
| `desimal` | double 64-bit — float 32-bit **tidak ada** (ditolak, lihat DEVIATIONS) |
| `logika` | `benar` / `salah` |
| `karakter` | 1 karakter (unicode diizinkan) |
| `teks` | string |
| `hampa` | unit/void |

### 3.2 Tipe Bahasa

- **`cacahan`** — enum (lihat §8.2); **`cacahan_sum`** — sum type (§8.3).
- **`struktur`** — record (§8.1), dengan dukungan **bitfield**
  (`field: bilangan : N`, lihat §8.4).
- **`Opsi(dengan T = T)`** — `ADA(nilai = v)` / `KOSONG()` (§10).
- **`Hasil(dengan T = T, E = teks)`** — `SUKSES(nilai = v)` / `GAGAL(pesan = e)`.
- **`larik[N] dari T`** / **`larik[2][3] dari T`** (multi-dim) / **`larik dari T`**
  (ukuran implisit — khusus parameter fungsi, output dinamis).
- **`tunjuk ke T`**, `tunjuk ke hampa` (§13).
- **Koleksi**: `Daftar`, `Antrian`, `Tumpukan`, `Iterator` (§12).
- **Numerik khusus**: `bilangan_tanpa_batas` (BTB), `desimal_tetap`,
  `bilangan_kompleks`, `matriks` (§15).
- **Konkurensi**: `Pekerja`, `Atomik`, `Mutex`, `Semaphore`, `Kondisi`,
  `Barrier`, `Saluran`, `Pembangkit` (§14).
- **Generik**: `generik T, E ... dimana T: Terbandingkan` (§7.3).

### 3.3 Instansiasi Generik

Dua notasi setara:

```rheinc
variabel q: Antrian<bilangan> = antrian()                  -- sudut
variabel d: Daftar(dengan T = bilangan, jenis = ganda) = daftar()  -- kata kunci
```

## 4. Deklarasi Variabel & Konstanta

```rheinc
variabel x: bilangan                          -- deklarasi
variabel y: bilangan = 0                      -- + inisialisasi
variabel z = a + b                            -- inferensi tipe
variabel w                                    -- tanpa init; diisi belakangan

tetap MAKS: bilangan = 100                    -- konstanta (global & lokal)

tugas_global variabel counter: Atomik(dengan T = bilangan) = atomik(0)  -- storage global
tugas_lokal  variabel cache: bilangan = 0                                 -- storage thread-local
```

## 5. Operator

| Kelompok | Operator |
|---|---|
| Aritmetika | `+ - * /` (trunc) `%` `//` (floor, lihat DEVIATIONS D-1) `**` (pangkat) |
| Bitwise | `& | ^ ~ << >>` |
| Logika | `dan atau bukan` |
| Perbandingan | `== != < > <= >=` |
| Increment | `++` `--` (bilangan, desimal, logika, karakter) |
| Assignment | `= += -= *= /= %= **= &= |= ^= <<= >>=` (RHS dievaluasi utuh) |
| Matriks | `@` (perkalian matriks) |
| Lainnya | `? :` ternary, `+>` append teks (`konten +> "x"`), `-> hasil` tangkap output-param, `->` akses member via pointer |

**Preseden** (dari `urutan_operasi.rhc`): `**` **right-assoc** dan terkuat
(`2 ** 3 ** 2 = 512`); lalu `* / %`; lalu `+ -`; lalu `<< >>`
(`1 + 2 << 2 = 12`); lalu perbandingan; lalu `&`; lalu `^`; lalu `|`;
terakhir `dan` lalu `atau`. Ternary ekspresi `jika .. maka .. selain ..`
(lihat §6.5).

## 6. Kontrol Alur

### 6.1 `jika` / `selain itu`

```rheinc
jika kondisi maka { ... }
selain itu jika kondisi2 maka { ... }      -- else-if
selain itu { ... }                          -- else (bentuk `selain itu { }` polos)
jika bukan semua diatas { ... }             -- else (bentuk mengikuti `maka`)
```

Kedua bentuk else (`selain itu { }` dan `jika bukan semua diatas { }`)
dipakai di korpus. Bentuk "menempel" di kurung tutup blok `maka` juga
sah (lihat `calc.rhc`):

```rheinc
jika b != 0 maka {
    ...
} jika bukan semua diatas {          -- else menempel di '}' blok maka
    ...
}
```

### 6.2 Perulangan

```rheinc
untuk i dari 1 sampai 10 { ... }            -- inkremental, inklusif
untuk k dari 0 sampai 10 langkah 2 { ... }  -- dengan langkah
selama kondisi { ... }                      -- while
ulangi { ... } selama kondisi               -- do-while
berhenti  lanjut                            -- break / continue
goto label                                  -- lompatan ke blok berlabel
label: { ... }
```

### 6.3 `pilih` (switch / pattern matching)

```rheinc
pilih ekspresi {                            -- ekspresi boleh kompleks: `pilih skor / 10`
    kasus 1:
    kasus 2: {                              -- kasus kosong = fall-through
        tampilkan "1 atau 2\n"
        berhenti
    }
    kasus 'B': { ... }                      -- karakter, integer, enum
    bawaan: { ... }                         -- default (opsional; tanpanya tidak cocok = no-op)
}
```

Kasus dapat mengikat payload untuk `cacahan_sum`/`Opsi`/`Hasil`:
`kasus ADA(nilai): { ... }`, `kasus KOSONG(): { ... }`.

### 6.4 `pilih` untuk `Opsi` / `Hasil`

```rheinc
pilih tarik(200) {
    kasus ADA(nilai): tampilkan nilai, "\n"
    kasus KOSONG():  tampilkan "gagal\n"
}
pilih hasil {
    kasus SUKSES(nilai): ...
    kasus GAGAL(pesan): ...
}
```

### 6.5 Ternary Ekspresi

```rheinc
variabel b = jika a > 10 maka 1000 selain 500
variabel grade = jika n >= 80 maka "A" selain jika n >= 60 maka "B" selain "C"
```

## 7. Fungsi

### 7.1 Definisi & Prototipe

```rheinc
fungsi tambah(a: bilangan, b: bilangan) -> bilangan { kembalikan a + b }
fungsi -> hampa { ...; kembalikan }          -- fungsi void eksplisit
prosedur sapa(nama: teks) { ... }           -- tanpa nilai balik

deklarasi fungsi tambah(a: bilangan, b: bilangan) -> bilangan;   -- prototipe
prototipe prosedur tampilkan_pesan(pesan: teks);                 -- prototipe
```

Prototipe memungkinkan **rekursi silang** (mutual recursion).

### 7.2 Parameter Output

Fungsi builtin/IO menangkap keluaran via `-> nama`:

```rheinc
sistem("uname -a") -> hasil
tulis_file(path, isi) -> sukses
gabung(a, b) -> c
```

### 7.3 Generik

```rheinc
generik T fungsi maks(a: T, b: T) -> T
dimana T: Terbandingkan {
    kembalikan a > b ? a : b
}
generik T, E cacahan_sum Hasil { kasus SUKSES(nilai: T), kasus GAGAL(pesan: E) }
generik T struktur Pasangan { kiri: T, kanan: T }
```

- Constraint: `dimana T: Terbandingkan` (bisa multi-parameter `generik T, E`).
- Instansiasi: `maks(dengan T = bilangan, 3, 4)`, `Pasangan(dengan T = teks)`,
  atau sudut `Antrian<bilangan>`.

### 7.4 Lambda & Fungsi sebagai Tipe

```rheinc
variabel jumlah = lambda(a: bilangan, b: bilangan) { kembalikan a + b }
tampilkan terapkan(lambda(a, b) { kembalikan a * b }, 6, 7), "\n"
variabel konstanta = lambda() { kembalikan 777 }

-- tipe fungsi sebagai parameter:
fungsi terapkan(f: tunjuk ke fungsi(a: bilangan, b: bilangan) -> bilangan,
                x: bilangan, y: bilangan) -> bilangan {
    kembalikan f(x, y)
}
```

Lambda bisa menjadi argumen (callback), disimpan di variabel, tanpa
parameter, satu baris, nested ternary, dsb.

## 8. Struktur & Enum

### 8.1 `struktur`

```rheinc
struktur Alamat { jalan: teks, kota: teks, kode_pos: bilangan }
struktur Mahasiswa {
    nama: teks,
    alamat_rumah: Alamat          -- nested struct
}
variabel m: Mahasiswa
m.alamat_rumah.jalan = "Jl. Merdeka No. 45"   -- akses berantai via `.`
variabel p: tunjuk ke Mahasiswa = alamat(mhs1)
p->jumlah_sks = p->jumlah_sks + 6               -- akses via pointer via `->`
```

Field boleh tipe generik (`Daftar(...)`, `Pembangkit(...)`, `matriks(...)`, ...).

### 8.2 `cacahan` (enum)

```rheinc
cacahan Warna { MERAH, HIJAU, BIRU }                    -- auto-numbering
cacahan Status { SUKSES = 0, GAGAL = -1, PENDING = 1 }  -- nilai eksplisit
cacahan Izin { BACA = 1, TULIS = 2, EKSEKUSI = 4 }      -- flag bitmask
```

### 8.3 `cacahan_sum` (sum type / algebraic)

```rheinc
generik T cacahan_sum Opsi
dimana T: Terbandingkan
{
    kasus ADA(nilai: T),
    kasus KOSONG()
}
generik T, E cacahan_sum Hasil {
    kasus SUKSES(nilai: T),
    kasus GAGAL(pesan: E)
}
cacahan_sum POHON {
    kasus DAUN(nilai: bilangan),
    kasus CABANG(anak: Opsi(POHON))
}
```

Kasus dirujuk sebagai `ADA(nilai)`, `KOSONG()`; pola `pilih` mengikat
nama payload. Enum bisa jadi nilai field lain:
`OPSI_BILANGAN = cacahan_sum(Opsi(dengan T = bilangan))`, dirujuk
`Daftar_Tipe.OPSI_BILANGAN`.

### 8.4 Bitfield

```rheinc
struktur Tanggal {
    hari: bilangan : 5,      -- 5 bit
    bulan: bilangan : 4,     -- 4 bit
    tahun: bilangan : 12     -- 12 bit
}
```

## 9. Larik & Slicing

### 9.1 Larik

```rheinc
variabel a: larik[5] dari bilangan
variabel m: larik[3][3] dari bilangan      -- multi-dimensi
variabel lit: larik[5] dari bilangan = [10, 20, 30, 40, 50]
variabel dyn: larik dari bilangan          -- ukuran implisit (param fungsi / output dinamis)
a[0] = 10
```

### 9.2 Slicing (`sampai`)

```rheinc
pilih a[1 sampai 3] { kasus ADA(slice): ... kasus KOSONG(): ... }   -- rentang [1..3]
pilih a[2 sampai] { ... }                                          -- sampai akhir
pilih a[0 sampai 4 langkah 2] { ... }                              -- berlangkah
pilih a[-3 sampai -1] { ... }                                       -- indeks negatif
pilih a[1 sampai 10] { kasus KOSONG(): tampilkan "Di luar batas
" } -- OOB -> KOSONG
a[1 sampai 3] = [99, 88, 77]                                       -- slice assignment
pilih m[0 sampai 1, 0 sampai 1] { ... }                              -- multi-dim
pilih d[1 sampai 2] { ... }                                          -- pada Daftar
```

Hasil slice bertipe `Opsi(...)` (view/iterator) — out-of-bounds,
`mulai > sampai`, atau pemisah tak ditemukan menghasilkan `KOSONG()`.
Catatan DEVIATIONS D-5: payload IR view vs tipe semantic `Opsi<elementType>`.

## 10. `Opsi` & `Hasil` (Error Handling)

```rheinc
variabel opt: Opsi(dengan T = bilangan) = ADA(nilai = 10)
variabel opt2: Opsi(dengan T = bilangan) = KOSONG()
variabel h: Hasil(dengan T = bilangan, E = teks) = SUKSES(nilai = 1)
-- GAGAL(pesan = "...")

tampilkan ambil_ada(opt), "\n"          -- ambil nilai ADA (tanpa pola)
pilih opt { kasus ADA(nilai): ... kasus KOSONG(): ... }
```

## 11. Koleksi Bawaan

| Tipe | Konstruktor | Fungsi (semua via `pilih` bila `Opsi`) |
|---|---|---|
| `Daftar(dengan T = T, jenis = tunggal\|ganda)` | `daftar()`, `daftar(jenis = ganda)`, `daftar_dari_larik(l)` | `daftar_tambah`, `daftar_tambah_depan`, `daftar_tambah_belakang`, `daftar_ukuran`, `daftar_ke`, `daftar_cari` -> Opsi(indeks), `daftar_hapus_depan`/`belakang` -> Opsi(nilai), `daftar_ke_larik`, `daftar_bersihkan`, `daftar_kosong` |
| `Antrian(dengan T = T)` | `antrian()` | `antrian_tambah`, `antrian_ukuran`, `antrian_ke`, `antrian_ambil` -> Opsi(nilai), `antrian_cari`, `antrian_ke_larik`, `antrian_bersihkan`, `antrian_kosong` |
| `Tumpukan(dengan T = T)` | `tumpukan()`, `tumpukan_dari_larik(l)` | `tumpukan_tambah`, `tumpukan_ukuran`, `tumpukan_ke`, `tumpukan_ambil` -> Opsi(nilai), `tumpukan_atas` -> Opsi(nilai), `tumpukan_cari`, `tumpukan_ke_larik`, `tumpukan_bersihkan`, `tumpukan_kosong` |
| `Iterator(dengan T = T)` | `iterator_mulai(coll)`, `iterator_akhir(coll)` | `iterator_berikutnya`, `iterator_sebelumnya`, `iterator_selesai` |

Semua koleksi bisa di-`untuk setiap` dan di-`alokasi` sebagai isi pointer.

## 12. Fungsi Larik & Teks

```rheinc
gabung(a, b) -> c                        -- gabung larik -> larik baru
urutkan(arr, kecil ke besar)             -- in-place, asc
urutkan(arr, besar ke kecil)             -- in-place, desc (teks case-sensitive)
potong(t, "awal" sampai "akhir")         -- potong teks in-place (inclusive)
potong(t, "awal")                        -- potong sampai akhir
```

## 13. Pointer & Memori

```rheinc
variabel ptr: tunjuk ke bilangan
ptr = alamat(nilai)                       -- address-of (`alamat x`)
isi(ptr) = 100                            -- dereference assignment
tampilkan isi(ptr), "\n"
variabel p2: tunjuk ke tunjuk ke bilangan = alamat(ptr)

dyn = alokasi(bilangan)                   -- heap
arr = alokasi(5 * ukuran(bilangan))       -- buffer mentah: arr[i] = ...
arr2 = alokasi_otomatis(bilangan, 5)      -- stack-like, auto-free saat fungsi selesai
ptr2 = realokasi(ptr, 2 * ukuran(bilangan))
salin(dst, src, 3 * ukuran(bilangan))     -- memcpy
tampilkan ukuran(bilangan64), ukuran(tunjuk ke bilangan)
bebaskan(ptr)                             -- manual free
kosong                                    -- null pointer
jika ptr == kosong maka { ... }
```

Pointer casting implisit antar `tunjuk ke` types (via `tunjuk ke hampa`).

## 14. Konkurensi

> Lihat `DEVIATIONS.md D-3`: lowering saat ini *sync-structural*
> (modifier async/coroutine diabaikan, `jeda` tanpa suspensi nyata).

### 14.1 Pekerja (thread), storage, primitif sinkronisasi

```rheinc
tugas_global variabel total: bilangan = 0          -- storage global
tugas_lokal  variabel cache: bilangan = 0          -- thread-local

variabel p: Pekerja(dengan T = bilangan) = buat_pekerja(fn, arg, ...)
pekerja_tunggu(p)                                   -- -> Opsi(T) bila T != hampa
pekerja_nama(p)  pekerja_id(p)  pekerja_status(p)
pekerja_prioritas(p, 5)
pekerja_gabung(a, b)  pekerja_lepas(p)  berhentikan_pekerja(p)  pekerja_tutup(p)

variabel m: Mutex = mutex()
mutex_kunci(m)  mutex_buka(m)
variabel a: Atomik(dengan T = bilangan) = atomik(0)
atomik_tambah(a, 1)  atomik_kurang(a, 1)  atomik_set(a, v)
atomik_baca(a)  atomik_tukar(a, v)
variabel s: Semaphore = semaphore(2)
semaphore_tunggu(s)  semaphore_lepas(s)
variabel c: Kondisi = kondisi()
kondisi_sinyal(c)                                   -- (dalam mutex)
variabel b: Barrier = barrier(3)
```

### 14.2 Saluran (channel)

```rheinc
variabel ch: Saluran(dengan T = bilangan) = saluran()
variabel ch2 = saluran(ukuran = 5)                  -- berbuffer
saluran_kirim(ch, 42)
pilih saluran_terima(ch) { kasus ADA(nilai): ... kasus KOSONG(): ... }  -- tutup -> KOSONG
saluran_tutup(ch)
saluran_ukuran(ch)
```

### 14.3 Async / Await

```rheinc
asinkron fungsi ambil_data(url: teks) -> teks { ... }
variabel t = ambil_data("https://contoh.com")        -- mulai tugas
variabel hasil = await t -> hasil                  -- tunggu + ambil
variabel gab = await gabung(a, b, c) -> hasil      -- jalan paralel semua
variabel cepat = await balapan(a, b) -> hasil      -- siapa selesai duluan
variabel h = mulai_tugas(fn, arg)
tidur(100)     batalkan(h)
```

### 14.4 Korutin & Pembangkit (generator)

```rheinc
coroutine fungsi pencetak(batas: bilangan) -> Pembangkit(dengan T = bilangan, R = hampa) {
    selama i < batas {
        hasil(i)                -- yield
        jeda                    -- suspend point
        i = i + 1
    }
}
pembangkit fungsi turun(n: bilangan) -> Pembangkit(...) {
    hasil dari turun(n - 1)     -- yield-from
}
untuk setiap x dalam pencetak(5) { ... }             -- iterate generator
selama bukan pembangkit_selesai(k) {
    pilih pembangkit_berikutnya(k) { kasus ADA(nilai): ... kasus KOSONG(): ... }
}
pembangkit_kirim(k, 42)  pembangkit_reset(k)
```

`jeda` dan `jeda sampai kondisi` juga tersedia di prosedur biasa.

## 15. Tipe Numerik Khusus

### 15.1 Bilangan Tanpa Batas (BTB)

```rheinc
variabel a: bilangan_tanpa_batas("12345678901234567890")
variabel b = btb("42")                     -- literal
variabel p = btb("2") ** btb("100")        -- semua operator & assignment
ke_teks(a)  ke_btb("123")
btb_gcd(a,b) btb_lcm(a,b) btb_faktorial(30) btb_akar(c)
btb_pangkat_mod(x,y,m) btb_prima(x) btb_bit_length(c)
variabel l: larik_btb[5] = [btb("10"), ...]
btb_ke_larik_btb(a)  larik_btb_ke_btb(l)
```

### 15.2 Desimal Tetap (fixed-point)

```rheinc
variabel a: desimal_tetap("123.45", skala = 2)
variabel c = dt("1000.00", skala = 2)
-- pembagian -> Opsi(dengan T = desimal_tetap)
ke_desimal_tetap("9999.99", skala = 2)
ke_desimal_tetap(btb("12345"), skala = 2)
ke_btb(dt1)  dt_akar  dt_pangkat  dt_abs  dt_bulat  dt_potong  dt_skala
variabel l: larik_dt[5] = [dt("10.00", skala = 2), ...]
```

### 15.3 Bilangan Kompleks

```rheinc
variabel a: bilangan_kompleks(basis = desimal, real = "3.14", imajiner = "2.71")
variabel c: bilangan_kompleks(basis = desimal_tetap, skala = 2, real = "3.14", imajiner = "2.71")
variabel e = kx(basis = desimal, real = "5.00", imajiner = "6.00")
-- pembagian -> Opsi
kx_modulus(a) kx_konjugat(a) ke_teks(a) ke_kompleks(s)
variabel l: larik_kd[3] / larik_kdt[2]
```

### 15.4 Matriks

```rheinc
variabel a: matriks(dengan T = bilangan, baris = 3, kolom = 3)
a = matriks(baris = 3, kolom = 3, [1, 2, 3, 4, 5, 6, 7, 8, 9])
variabel t = a.transpose()        variabel det = a.det()        -- -> Opsi
a.invers() a.trace() a.rank() a.lu() a.qr()                     -- -> Opsi/ nilai
variabel k = a @ b                -- matmul
variabel it = a.iterasi()         -- it.selesai() it.berikutnya() -> pos.baris/.kolom/.nilai
variabel m3d: matriks(dengan T = bilangan, dimensi = (2, 3, 4))
variabel lm: larik_matriks[2] = [a, b]
larik_matriks_ke_matriks(lm)
```

## 16. I/O, Sistem & Sinyal

### 16.1 I/O dasar

```rheinc
tampilkan "a = ", a, ", b = ", b, "\n"    -- multi-argumen
baca(umur)                                  -- baca ke variabel
baca("Masukkan nama: ", nama)               -- dengan prompt
tulis_file(path, isi) -> sukses
baca_file(path, isi) -> sukses
hapus_file(path) -> sukses
```

### 16.2 Perintah sistem

```rheinc
sistem("uname -a") -> hasil                 -- tangkap stdout perintah shell
```

### 16.3 Signal handling

```rheinc
tangkap_sinyal(SIGINT) { tangani_sigint(SIGINT) }   -- handler dalam blok
abaikan_sinyal(SIGPIPE)
kembalikan_sinyal(SIGUSR1)
blokir_sinyal(SIGUSR1)  buka_blokir_sinyal(SIGUSR1)
kirim_sinyal(pid_sendiri(), SIGUSR2)
tunggu_sinyal(SIGINT)
reset_sinyal(SIGINT)
keluar(0)                                    -- keluar program dengan kode
```

Tersedia juga di fungsi/korutin; handler bertipe `fungsi(Sinyal) -> hampa`.

## 17. Konversi Tipe

```rheinc
ke_teks(x)        -- bilangan, desimal, BTB, logika, kompleks, dsb.
ke_bilangan(s)    ke_desimal(s)     ke_logika(x)     ke_karakter(65)
ke_btb(s)         ke_desimal_tetap(x, skala = n)   ke_kompleks(s)
```

- Casting implisit primitif berlaku (bilangan↔desimal↔logika↔karakter).
- Casting overflow dibungkus modulo width (contoh: `bilangan8 = 1000000`).
- String invalid -> default (`ke_bilangan("abc") = 0`, `ke_desimal("abc") = 0.0`).

## 18. Preprocessor (`rhpp/`)

File header `.rhh` dimuat dengan `#tambahkan`:

```rheinc
#tambahkan "deklarasi.rhh"          -- include; include ganda aman (idempotent)
#tambahkan "circular_a.rhh"       -- deteksi siklus (A<->B) -> error
```

### 18.1 Definisi makro

```rheinc
#definisikan FLAG_DEBUG                       -- objek (tanpa nilai)
#definisikan VERSION 1                        -- objek bernilai
#definisikan MAX(a, b) ((a) > (b) ? (a) : (b))   -- fungsi
#definisikan LOG(pesan, ...) tampilkan(pesan __VA_OPT__(,) __VA_ARGS__)  -- variadik
#definisikan STR(x) #x                       -- stringize
#definisikan CONCAT(a, b) a##b               -- token paste
#definisikan LONG_MACRO \
    tampilkan "banyak\n" \
    tampilkan "baris\n"                     -- lanjutan baris dengan backslash
#hapus definisi TEST                         -- undef
#definisikan NILAI 1
#definisikan NILAI 2                        -- redefinisi -> ERROR (variadic
                                            -- tanpa `...` pakai __VA_ARGS__ -> ERROR)
```

### 18.2 Kondisional preprocessor

```rheinc
#jika FLAG_DEBUG terdefinisi     ... #akhir
#jika FLAG_RELEASE tidak terdefinisi  ... #akhir
#jika VERSION > 0                ... #akhir
#jika MODE == 1  ... #jika MODE == 2  ... #jika bukan semua diatas  ... #akhir
#jika (EXPR_A + EXPR_B) == 9     ... #akhir     -- ekspresi penuh: + - * / % & | ^
                                                -- << >> dan atau bukan ? : == !=
#jika UNDEFINED_EXPR == 0        ... #akhir     -- identifier tak terdefinisi = 0
```

- Ekspresi `#jika` bisa memakai makro fungsi yang **sudah** diekspansi:
  `#jika MAX(1, 2) == 2`, `#jika MAX terdefinisi`, `#jika TIDAK_ADA tidak terdefinisi`.
- Directive di dalam cabang `#jika` tidak aktif **tidak diproses** sama sekali.
- `#error "pesan"` (abort), `#warning "pesan"` (lanjut).

### 18.3 Variadik

`__VA_ARGS__` (semua argumen sisa), `__VA_OPT__(...)` (hanya bila ada argumen
sisa — bisa bersarang). Pembatasan: `__VA_ARGS__`/`__VA_OPT__` hanya sah di
makro yang punya `...`.

## 19. Keyword Bahasa (ringkasan)

`variabel`, `tetap`, `tugas_global`, `tugas_lokal`, `fungsi`, `prosedur`,
`deklarasi`, `prototipe`, `kembalikan`, `struktur`, `cacahan`, `cacahan_sum`,
`generik`, `dimana`, `jika`, `maka`, `selain`, `selain itu`, `jika bukan
semua diatas`, `untuk`, `dari`, `sampai`, `langkah`, `setiap`, `dalam`,
`selama`, `ulangi`, `pilih`, `kasus`, `bawaan`, `berhenti`, `lanjut`,
`goto`, `jamin`, `saat`, `keluar`, `asinkron`, `await`, `coroutine`,
`pembangkit`, `hasil`, `jeda`, `tunda`, `dan`, `atau`, `bukan`, `benar`,
`salah`, `kosong`, `dengan`, `tunjuk`, `ke`, `alamat`, `isi`, `alokasi`,
`alokasi_otomatis`, `realokasi`, `bebaskan`, `ukuran`, `larik`, `dari`,
`logika`, `bilangan`, `desimal`, `karakter`, `teks`, `hampa`.

---

*Disusun dari korpus `sintaks-awal/` — 81 file `.rhc` (73 program positif,
8 uji negatif: `lexer_error/*`, `rhpp/*error*`) + 5 `.rhh` di `rhpp/`.*
