; Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
; You should have received a copy of the GNU Affero General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.
;
; AOC 2024 Day 3 Part 2: as part 1, but do() and don't() switch muls on and off (initially on)
;
; Build (Windows, MSVC linker):   nasm -f win64 Part2.asm -o Part2.obj
;                                 link /subsystem:console /entry:main /nodefaultlib Part2.obj kernel32.lib
; Build (Windows, MinGW):         nasm -f win64 Part2.asm -o Part2.obj
;                                 gcc -nostartfiles -o Part2.exe Part2.obj -lkernel32 -Wl,-e,main
; Build (Linux, for testing):     nasm -f elf64 -DLINUX Part2.asm -o Part2.o && ld -o part2 Part2.o
; Run:  Part2.exe path | Part2.exe "<raw data>" | Part2.exe   (input.txt, ../input.txt, ../../input.txt,
;       2024/DAY03/input.txt). An argument that opens as a file is a path, anything else is data
; needs AVX2, BMI1, BMI2 and POPCNT Haswell / Zen1 or newer
;
; Intuition
;
; A state machine flipping an enable flag would put a data dependent branch back in the hot loop, so
; we look at the problem from the other side. Whether a mul counts depends only on the last event
; before it, and events are rare (62 in 18 KB against 672 muls). So we do not gate muls at all. Every
; mul is evaluated exactly like in part 1 and we keep a prefix sum P over the mul list in position
; order. The answer is then P[total] minus whatever sits inside disabled intervals, where a disabled
; interval runs from a don't() that finds the state enabled to the next do() (or to the end). Each
; interval is worth P[r(do)] - P[r(dont)], with r(x) being the number of muls starting before byte x.
;
; r(x) needs no searching at all. The scan already produces a 64 bit mul candidate mask per 64 byte
; block, so we just store that mask and the running candidate count before the block. Then r(x) is
; count_before[x >> 6] plus the popcount of mask[x >> 6] truncated below bit (x & 63), a single bzhi.
; Events are found with the same flatten trick as muls: a cmpeq against 'd' gives a mask (in the real
; input every 'd' is an event), positions go to a list through unconditional stores, and each entry is
; classified by two plain compares against "do()" and "don't()". The tiny state machine (enabled flag,
; start of the current disabled interval, accumulated subtraction) uses cmov and multiplication by 0/1
; so it is branch free as well. The mul parse is the same SWAR routine as part 1: non digit flags
; from a byte >= 10 test, tzcnt for both lengths, shifts for zero padding, one multiply by
; (1 + 100 * 2^16) to produce both numbers, cmov-to-zero for validity.

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
event_pos       resd BUFFER_SIZE + 64
block_mask      resq (BUFFER_SIZE >> 6) + 2
block_count     resd (BUFFER_SIZE >> 6) + 2
prefix_sum      resq MAX_CANDIDATES + 2
total_all       resq 1
path_buffer     resb 512
output_buffer   resb 64

section .text

%ifdef LINUX
; Linux test harness only
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
        mov     rax, 'Part 2: '
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

;  solve: r12 = length, returns rax
%define list_cursor             rdi
%define event_cursor            r15
%define mask_store              r13
%define count_store             r11
%define candidate_total         ebx
%define block_offset            rcx
%define block_offset_32         ecx
%define hits                    r8
%define hits_32                 r8d
%define event_hits              r9
%define event_hits_32           r9d
%define slot                    r10
%define slot_32                 r10d

solve:
        lea     text, [buffer]
        lea     list_cursor, [candidate_pos]
        lea     event_cursor, [event_pos]
        lea     mask_store, [block_mask]
        lea     count_store, [block_count]
        mov     eax, 'm'
        vmovd   xmm14, eax
        vpbroadcastb ymm14, xmm14
        mov     eax, '('
        vmovd   xmm15, eax
        vpbroadcastb ymm15, xmm15
        mov     eax, 'd'
        vmovd   xmm13, eax
        vpbroadcastb ymm13, xmm13
        xor     candidate_total, candidate_total
        xor     block_offset_32, block_offset_32
.block_loop:
        vmovdqu ymm0, [text + block_offset]
        vmovdqu ymm2, [text + block_offset + 32]
        vpcmpeqb ymm4, ymm0, ymm14              ; 'm' in low half
        vpcmpeqb ymm5, ymm2, ymm14              ; 'm' in high half
        vpcmpeqb ymm6, ymm0, ymm13              ; 'd' in low half
        vpcmpeqb ymm7, ymm2, ymm13              ; 'd' in high half
        vpcmpeqb ymm1, ymm15, [text + block_offset + 3]
        vpcmpeqb ymm3, ymm15, [text + block_offset + 35]
        vpand   ymm4, ymm4, ymm1
        vpand   ymm5, ymm5, ymm3
        vpmovmskb eax, ymm4
        vpmovmskb edx, ymm5
        shl     rdx, 32
        or      rax, rdx                        ; rax = mul candidate mask
        vpmovmskb edx, ymm6
        vpmovmskb ebp, ymm7
        shl     rbp, 32
        or      rdx, rbp                        ; rdx = 'd' mask
        mov     [mask_store], rax
        mov     [count_store], candidate_total
        add     mask_store, 8
        add     count_store, 4
        popcnt  hits, rax
        popcnt  event_hits, rdx
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
        tzcnt   slot, rdx
        blsr    rdx, rdx
        add     slot_32, block_offset_32
        mov     [event_cursor], slot_32
        tzcnt   slot, rdx
        blsr    rdx, rdx
        add     slot_32, block_offset_32
        mov     [event_cursor + 4], slot_32
        cmp     event_hits_32, 2
        ja      .extra_events
.events_done:
        add     candidate_total, hits_32
        lea     list_cursor, [list_cursor + hits * 4]
        lea     event_cursor, [event_cursor + event_hits * 4]
        add     block_offset_32, 64
        cmp     block_offset_32, r12d
        jb      .block_loop
        vzeroupper

        ; prefix sums over the per mul values, P[0] = 0
        mov     parse_end, list_cursor
        mov     r11, event_cursor               ; end of the event list, kept for later
        lea     parse_cursor, [candidate_pos]
        lea     r10, [prefix_sum]
        xor     zero_reg, zero_reg
        xor     r14d, r14d                      ; running sum
        cmp     parse_cursor, parse_end
        jae     .parse_done
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
        add     r10, 8
        mov     [r10], r14
        cmp     parse_cursor, parse_end
        jb      .parse_loop
.parse_done:
        mov     [total_all], r14

        ;      walk the events: r13 cursor, r11 end, rbx prefix table, r8 masks, r9 counts
        ;      rbp enabled (0/1), r10 prefix at the last don't(), r12 subtracted total
        lea     r13, [event_pos]
        lea     rbx, [prefix_sum]
        lea     r8, [block_mask]
        lea     r9, [block_count]
        mov     ebp, 1
        xor     r10d, r10d
        xor     r12d, r12d
        mov     r15, 0x292874276E6F6400         ; "don't()" shifted up by one byte
        cmp     r13, r11
        jae     .events_finished
.event_loop:
        mov     ecx, [r13]                      ; byte position of a 'd'
        add     r13, 4
        mov     rdx, [text + rcx]
        xor     edi, edi
        cmp     edx, 0x29286F64                 ; "do()"
        sete    dil                             ; is_do
        shl     rdx, 8
        xor     r14d, r14d
        cmp     rdx, r15
        sete    r14b                            ; is_dont
        mov     eax, ecx
        shr     eax, 6                          ; block index
        and     ecx, 63
        mov     rdx, [r8 + rax * 8]             ; mul mask of that block
        mov     eax, [r9 + rax * 4]             ; muls before that block
        bzhi    rdx, rdx, rcx                   ; keep candidates strictly before the event
        popcnt  rdx, rdx
        add     eax, edx                        ; r(x): muls starting before the event
        mov     rax, [rbx + rax * 8]            ; P[r(x)]
        mov     ecx, r14d
        and     ecx, ebp                        ; don't() while enabled
        test    ecx, ecx
        cmovnz  r10, rax                        ; remember where the disabled stretch begins
        mov     edx, ebp
        xor     edx, 1
        and     edx, edi                        ; do() while disabled
        mov     rcx, rax
        sub     rcx, r10
        imul    rcx, rdx
        add     r12, rcx                        ; close the stretch
        xor     r14d, 1
        and     ebp, r14d
        or      ebp, edi                        ; enabled = is_do | (enabled & !is_dont)
        cmp     r13, r11
        jb      .event_loop
.events_finished:
        mov     rax, [total_all]
        mov     rcx, rax
        sub     rcx, r10
        mov     edx, ebp
        xor     edx, 1
        imul    rcx, rdx                        ; a trailing disabled stretch runs to the end
        add     r12, rcx
        sub     rax, r12
        ret

.extra_hits:                                    ; cold: more than four candidates in one block
        lea     rbp, [list_cursor + 16]
        lea     r14d, [hits - 4]
.extra_loop:
        tzcnt   slot, rax
        blsr    rax, rax
        add     slot_32, block_offset_32
        mov     [rbp], slot_32
        add     rbp, 4
        dec     r14d
        jnz     .extra_loop
        jmp     .hits_done

.extra_events:                                  ; cold: more than two 'd' bytes in one block
        lea     rbp, [event_cursor + 8]
        lea     r14d, [event_hits - 2]
.extra_event_loop:
        tzcnt   slot, rdx
        blsr    rdx, rdx
        add     slot_32, block_offset_32
        mov     [rbp], slot_32
        add     rbp, 4
        dec     r14d
        jnz     .extra_event_loop
        jmp     .events_done