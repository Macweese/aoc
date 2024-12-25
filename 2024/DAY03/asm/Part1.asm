; Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
; You should have received a copy of the GNU Affero General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.
;
; AOC 2024 Day 3 Part 1: sum of every well formed mul(X,Y), X and Y 1-3 digits
;
; Build (Windows, MSVC linker):   nasm -f win64 Part1.asm -o Part1.obj
;                                 link /subsystem:console /entry:main /nodefaultlib Part1.obj kernel32.lib
; Build (Windows, MinGW):         nasm -f win64 Part1.asm -o Part1.obj
;                                 gcc -nostartfiles -o Part1.exe Part1.obj -lkernel32 -Wl,-e,main
; Build (Linux, for testing):     nasm -f elf64 -DLINUX Part1.asm -o Part1.o && ld -o part1 Part1.o
; Run:  Part1.exe path | Part1.exe "<raw data>" | Part1.exe   (input.txt, ../input.txt, ../../input.txt,
;       2024/DAY03/input.txt). An argument that opens as a file is a path, anything else is data
; Needs AVX2, BMI1, BMI2 and POPCNT ~~ Haswell / Zen1 or newer
;
; Intuition
;
; The floor for this problem is one pass over the bytes, so what matters is what each byte costs
; Regexes and byte-at-a-time state machines pay a data dependent branch per byte, and about one
; in five 'm' characters is a dead end (mul(4*, mul[3,7], ...), so those branches mispredict all
; day. Here the work is split into two stages that are both essentially branch free
;
; Stage one only finds candidates. For every 64 byte block we compare against 'm' and against '('
; three bytes later, AND the two and squeeze the result into a 64 bit mask. Testing two of the four
; bytes of "mul(" is on purpose: false positives are negligible, and the remaining two bytes get
; verified for free later with one dword compare. The hits are flattened into a u32 position list
; with four unconditional stores and a popcount advance, so the ~2.4 hits per block cost no
; branches. The only branch (more than four hits) is a cold path
;
; Stage two parses each candidate with pure arithmetic. Everything after "mul(" fits in eight bytes
; since the longest legal tail is "123,456)". Xor with 0x30 per byte turns digits into 0..9 and
; everything else into 10 or more, a SWAR "byte >= 10" test flags the non digits, and tzcnt of the
; flags is 8 * len1 + 7 while tzcnt after clearing the lowest set bit gives the second stop
; Validity is: both lengths in 1..3, a comma at the first stop, ')' at the second, and the "mul("
; prefix, each one a cmp plus cmov-to-zero so a bad candidate just contributes 0. The shifts double
; as leading zero padding: the first number lands at the top of the low 32 bit lane and the second at
; the top of the high lane, four digit bytes each. Then y * 10 + (y >> 8) folds neighbouring digits
; into two digit values in bytes 0,2,4,6 (all <= 99, so no carries leak), a mask keeps them, and a
; single multiply by (1 + 100 * 2^16) drops 100 * hi + lo at bit 16 and bit 48, which are exactly X
; and Y. One imul for X * Y and we are done. No division, no digit loops, no libc

%ifdef LINUX
bits 64
default rel
global _start
%define RODATA .rodata
%else
bits 64
default rel
extern GetCommandLineA
extern CreateFileA
extern ReadFile
extern GetStdHandle
extern WriteFile
extern ExitProcess
global main
%define RODATA .rdata
%endif

BUFFER_SIZE equ 1 << 20
MAX_CANDIDATES equ (BUFFER_SIZE >> 2) + 64

section RODATA align=16
ascii_bias      dq 0x3030303030303030
nondigit_bias   dq 0x7676767676767676
high_bits       dq 0x8080808080808080
pair_mask       dq 0x00FF00FF00FF00FF
path_local      db "input.txt", 0
path_parent     db "../input.txt", 0
path_grand      db "../../input.txt", 0
path_tree       db "2024/DAY03/input.txt", 0

section .bss align=64
buffer          resb BUFFER_SIZE + 192
candidate_pos   resd MAX_CANDIDATES

path_buffer     resb 512
output_buffer   resb 64

section .text

%ifdef LINUX
;; Linux test harness only
open_and_read:                          ; rdi = path, returns rax = length or -1
        mov     eax, 2
        xor     esi, esi
        xor     edx, edx
        syscall
        test    rax, rax
        js      .failed
        mov     rdi, rax
        xor     eax, eax
        lea     rsi, [buffer]
        mov     edx, BUFFER_SIZE
        syscall
        test    rax, rax
        jns     .done
.failed:
        mov     rax, -1
.done:
        ret

_start:
        mov     rax, [rsp]
        cmp     rax, 2
        jb      .no_argument
        mov     r13, [rsp + 16]
        mov     rdi, r13
        call    open_and_read
        cmp     rax, -1
        jne     .loaded
        mov     rsi, r13                ; not a file: treat the argument as raw data
        lea     rdi, [buffer]
        xor     ecx, ecx
.copy_raw:
        movzx   eax, byte [rsi + rcx]
        test    eax, eax
        jz      .raw_done
        mov     [rdi + rcx], al
        inc     ecx
        cmp     ecx, BUFFER_SIZE
        jb      .copy_raw
.raw_done:
        mov     eax, ecx
        jmp     .loaded
.no_argument:
        lea     rdi, [path_local]
        call    open_and_read
        cmp     rax, -1
        jne     .loaded
        lea     rdi, [path_parent]
        call    open_and_read
        cmp     rax, -1
        jne     .loaded
        lea     rdi, [path_grand]
        call    open_and_read
        cmp     rax, -1
        jne     .loaded
        lea     rdi, [path_tree]
        call    open_and_read
        cmp     rax, -1
        jne     .loaded
        mov     edi, 1
        mov     eax, 231
        syscall
.loaded:
        mov     r12, rax
        call    solve
%else
; Win x64
open_and_read:                          ; rcx = path, returns rax = length or -1
        sub     rsp, 0x58
        mov     edx, 0x80000000         ; GENERIC_READ
        mov     r8d, 1                  ; FILE_SHARE_READ
        xor     r9d, r9d
        mov     dword [rsp + 32], 3     ; OPEN_EXISTING
        mov     dword [rsp + 40], 0x80  ; FILE_ATTRIBUTE_NORMAL
        mov     qword [rsp + 48], 0
        call    CreateFileA
        cmp     rax, -1
        je      .done
        mov     rcx, rax
        lea     rdx, [buffer]
        mov     r8d, BUFFER_SIZE
        lea     r9, [rsp + 64]
        mov     qword [rsp + 32], 0
        call    ReadFile
        mov     eax, [rsp + 64]
.done:
        add     rsp, 0x58
        ret

main:
        sub     rsp, 0x58
        call    GetCommandLineA
        mov     rsi, rax
        cmp     byte [rsi], '"'         ; skip the program name (quoted or not)
        jne     .name_plain
        inc     rsi
.name_quoted:
        movzx   eax, byte [rsi]
        test    eax, eax
        jz      .no_argument
        inc     rsi
        cmp     eax, '"'
        jne     .name_quoted
        jmp     .skip_spaces
.name_plain:
        movzx   eax, byte [rsi]
        test    eax, eax
        jz      .no_argument
        cmp     eax, ' '
        je      .skip_spaces
        inc     rsi
        jmp     .name_plain
.skip_spaces:
        cmp     byte [rsi], ' '
        jne     .have_argument
        inc     rsi
        jmp     .skip_spaces
.have_argument:
        cmp     byte [rsi], 0
        je      .no_argument
        xor     r8d, r8d
        cmp     byte [rsi], '"'
        jne     .measure
        inc     rsi
        mov     r8d, 1
.measure:
        mov     rdi, rsi
.scan_end:
        cmp     byte [rdi], 0
        je      .scan_done
        inc     rdi
        jmp     .scan_end
.scan_done:
        test    r8d, r8d
        jz      .length_known
        cmp     rdi, rsi
        jbe     .length_known
        cmp     byte [rdi - 1], '"'
        jne     .length_known
        dec     rdi
.length_known:
        mov     r13, rsi                ; argument start
        mov     r12, rdi
        sub     r12, rsi                ; argument length
        lea     rdi, [path_buffer]
        mov     ecx, r12d
        cmp     ecx, 511
        jbe     .path_copy
        mov     ecx, 511
.path_copy:
        rep movsb
        mov     byte [rdi], 0
        lea     rcx, [path_buffer]
        call    open_and_read
        cmp     rax, -1
        jne     .loaded
        mov     rsi, r13                ; not a file: treat the argument as raw data
        lea     rdi, [buffer]
        mov     rcx, r12
        cmp     rcx, BUFFER_SIZE
        jbe     .raw_copy
        mov     ecx, BUFFER_SIZE
.raw_copy:
        mov     rax, rcx
        rep movsb
        jmp     .loaded
.no_argument:
        lea     rcx, [path_local]
        call    open_and_read
        cmp     rax, -1
        jne     .loaded
        lea     rcx, [path_parent]
        call    open_and_read
        cmp     rax, -1
        jne     .loaded
        lea     rcx, [path_grand]
        call    open_and_read
        cmp     rax, -1
        jne     .loaded
        lea     rcx, [path_tree]
        call    open_and_read
        cmp     rax, -1
        jne     .loaded
        mov     ecx, 1
        call    ExitProcess
.loaded:
        mov     r12, rax
        call    solve
%endif

; print "Part N: <rax>\n"
        lea     rdi, [output_buffer + 31]
        mov     byte [rdi], 10
        mov     r8, 0xCCCCCCCCCCCCCCCD  ; ceil(2^67 / 10): n / 10 == mulhi(n, magic) >> 3
.next_digit:
        mov     rcx, rax
        mul     r8
        shr     rdx, 3
        lea     rax, [rdx + rdx * 4]
        add     rax, rax
        sub     rcx, rax
        add     cl, '0'
        dec     rdi
        mov     [rdi], cl
        mov     rax, rdx
        test    rax, rax
        jnz     .next_digit
        sub     rdi, 8
        mov     rax, 'Part 1: '
        mov     [rdi], rax
%ifdef LINUX
        lea     rdx, [output_buffer + 32]
        sub     rdx, rdi
        mov     rsi, rdi
        mov     edi, 1
        mov     eax, 1
        syscall
        xor     edi, edi
        mov     eax, 231
        syscall
%else
        mov     ecx, -11
        call    GetStdHandle
        mov     rcx, rax
        mov     rdx, rdi
        lea     r8, [output_buffer + 32]
        sub     r8, rdi
        lea     r9, [rsp + 64]
        mov     qword [rsp + 32], 0
        call    WriteFile
        xor     ecx, ecx
        call    ExitProcess
%endif

%define text                    rsi
%define zero_reg                rdi
%define parse_cursor            r13
%define parse_end               r12
%define tail                    rdx
%define digit_values            rax
%define digit_values_32         eax
%define first_len_bits          r15
%define first_len_bits_32       r15d
%define second_stop_bits        rbp
%define second_stop_bits_32     ebp
%define prefix_diff_32          r8d
%define scratch_a               rbx
%define scratch_a_32            ebx
%define scratch_a_8             bl
%define scratch_b               rcx
%define scratch_b_32            ecx
%define scratch_b_16            cx

; solve: r12 = length, returns rax
%define list_cursor             rdi
%define block_offset            rcx
%define block_offset_32         ecx
%define hits                    r8
%define hits_32                 r8d
%define slot                    r9
%define slot_32                 r9d

solve:
        lea     text, [buffer]
        lea     list_cursor, [candidate_pos]
        mov     eax, 'm'
        vmovd   xmm14, eax
        vpbroadcastb ymm14, xmm14
        mov     eax, '('
        vmovd   xmm15, eax
        vpbroadcastb ymm15, xmm15
        xor     block_offset_32, block_offset_32
.block_loop:
        vmovdqu ymm0, [text + block_offset]
        vmovdqu ymm2, [text + block_offset + 32]
        vpcmpeqb ymm0, ymm0, ymm14
        vpcmpeqb ymm2, ymm2, ymm14
        vpcmpeqb ymm1, ymm15, [text + block_offset + 3]
        vpcmpeqb ymm3, ymm15, [text + block_offset + 35]
        vpand   ymm0, ymm0, ymm1
        vpand   ymm2, ymm2, ymm3
        vpmovmskb eax, ymm0
        vpmovmskb edx, ymm2
        shl     rdx, 32
        or      rax, rdx                        ; 64 bit candidate mask
        popcnt  hits, rax
        tzcnt   slot, rax
        blsr    rax, rax
        add     slot_32, block_offset_32
        mov     [list_cursor], slot_32
        tzcnt   slot, rax
        blsr    rax, rax
        add     slot_32, block_offset_32
        mov     [list_cursor + 4], slot_32
        tzcnt   slot, rax
        blsr    rax, rax
        add     slot_32, block_offset_32
        mov     [list_cursor + 8], slot_32
        tzcnt   slot, rax
        blsr    rax, rax
        add     slot_32, block_offset_32
        mov     [list_cursor + 12], slot_32
        cmp     hits_32, 4
        ja      .extra_hits
.hits_done:
        lea     list_cursor, [list_cursor + hits * 4]
        add     block_offset_32, 64
        cmp     block_offset_32, r12d
        jb      .block_loop
        vzeroupper

        lea     parse_cursor, [candidate_pos]
        mov     parse_end, list_cursor
        xor     zero_reg, zero_reg
        xor     r14d, r14d                      ; running sum
        cmp     parse_cursor, parse_end
        jae     .finished
.parse_loop:
        mov     ecx, [parse_cursor]                     ; position of the 'm'
        add     parse_cursor, 4
        mov     tail, [text + rcx + 4]                  ; the 8 bytes after "mul("
        mov     prefix_diff_32, [text + rcx]
        xor     prefix_diff_32, 0x286C756D              ; zero iff the bytes are exactly "mul("
        mov     digit_values, tail
        xor     digit_values, [ascii_bias]              ; digits become 0..9, the rest >= 10
        mov     second_stop_bits, digit_values
        add     second_stop_bits, [nondigit_bias]
        or      second_stop_bits, digit_values
        and     second_stop_bits, [high_bits]           ; 0x80 in every non digit byte
        tzcnt   first_len_bits, second_stop_bits        ; 8 * len1 + 7
        blsr    second_stop_bits, second_stop_bits
        tzcnt   second_stop_bits, second_stop_bits      ; 8 * (len1 + len2 + 1) + 7
        and     first_len_bits_32, -8                   ; 8 * len1
        and     second_stop_bits_32, -8                 ; 8 * (len1 + len2 + 1)
        lea     scratch_a, [first_len_bits + 8]
        shrx    scratch_b, digit_values, scratch_a      ; second number, digits in the low bytes
        sub     scratch_a, second_stop_bits
        shlx    scratch_b, scratch_b, scratch_a         ; pushed to the top of the upper lane
        mov     scratch_a_32, first_len_bits_32
        neg     scratch_a_32
        shlx    digit_values_32, digit_values_32, scratch_a_32 ; first number, top of the lower lane
        or      digit_values, scratch_b                 ; both numbers, four digit bytes each
        lea     scratch_b, [digit_values + digit_values * 4]
        mov     scratch_a, digit_values
        shr     scratch_a, 8
        lea     digit_values, [scratch_a + scratch_b * 2] ; 10 * y + (y >> 8): pairs of digits
        and     digit_values, [pair_mask]
        imul    digit_values, digit_values, 0x640001    ; 100 * hi + lo at bit 16 and bit 48
        mov     scratch_b, digit_values
        shr     scratch_b, 16
        movzx   scratch_b_32, scratch_b_16
        shr     digit_values, 48
        imul    digit_values, scratch_b                 ; X * Y
        test    prefix_diff_32, prefix_diff_32
        cmovnz  digit_values, zero_reg
        lea     scratch_a, [first_len_bits - 8]
        cmp     scratch_a, 16
        cmova   digit_values, zero_reg
        lea     scratch_a, [second_stop_bits - 16]
        sub     scratch_a, first_len_bits
        cmp     scratch_a, 16
        cmova   digit_values, zero_reg
        shrx    scratch_a, tail, first_len_bits
        cmp     scratch_a_8, ','
        cmovne  digit_values, zero_reg
        shrx    scratch_a, tail, second_stop_bits
        cmp     scratch_a_8, ')'
        cmovne  digit_values, zero_reg
        add     r14, digit_values
        cmp     parse_cursor, parse_end
        jb      .parse_loop
.finished:
        mov     rax, r14
        ret

.extra_hits:                                    ; cold: more than four candidates in one block
        lea     r10, [list_cursor + 16]
        lea     r11d, [hits - 4]
.extra_loop:
        tzcnt   slot, rax
        blsr    rax, rax
        add     slot_32, block_offset_32
        mov     [r10], slot_32
        add     r10, 4
        dec     r11d
        jnz     .extra_loop
        jmp     .hits_done