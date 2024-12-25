; Copyright (C) 2024 Macweese <https://www.github.com/Macweese>, <hugo.git@pm.me> <hugo.n@linux.com>
; You should have received a copy of the GNU Affero General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.
;
; AOC 2024 Day 2 Part 1
; count of safe reports
;
; Build (Windows x64, no CRT, only kernel32):
;   nasm -f win64 Part1.asm -o Part1.obj
;   link /nologo /subsystem:console /entry:start /nodefaultlib /fixed Part1.obj kernel32.lib
;   or (MinGW):  gcc -nostartfiles -Wl,-e,start -o Part1.exe Part1.obj -lkernel32
; Run:  Part1.exe "<raw input>"     arg starting with a digit (after an optional quote) is DATA
;       Part1.exe path              any other arg is a PATH
;       Part1.exe                   input.txt ..\input.txt ..\..\input.txt 2024\DAY02\input.txt
; Needs SSE2 + BMI1 + POPCNT (Haswell / Zen1 or newer).
;
; Algorithm: see Part1.c. One 32-byte window per line, branch-free tokenizer
; (separator bitmask, tzcnt line length, pmullw*10 digit fold, 8 fixed tzcnt/blsr slots),
; levels packed one per byte into r12, safety decided by SIMD byte ops on that single register.
; safe <=> all n-1 good bits set in the inc half or the dec half of one pmovmskb.
bits 64
default rel

%define FRONT 64
%define BUFSZ (1 << 20)

%ifndef LINUXTEST
extern __imp_GetCommandLineA
extern __imp_CreateFileA
extern __imp_ReadFile
extern __imp_WriteFile
extern __imp_GetStdHandle
extern __imp_ExitProcess
global start
%else
global solve_test
%endif

section .rdata align=16
align 16
k_ch0:	times 16 db '0'
k_nl:	times 16 db 10
k_ten:	times 8 dw 10
k_two:	times 16 db 2
k_adj:	times 8 db 0xFF
	times 8 db 3
%ifndef LINUXTEST
p_0:	db "input.txt", 0
p_1:	db "..\input.txt", 0
p_2:	db "..\..\input.txt", 0
p_3:	db "2024\DAY02\input.txt", 0
%endif

section .bss align=64
buf:	resb FRONT + BUFSZ + 128
align 64
scr:	resb 64
pathb:	resb 512
outb:	resb 32
gotn:	resd 1

section .text

; in : rsi = data (>= 64 zero/non-digit bytes before it, '\n' sentinel at rdi), rdi = data + len
; out: eax = answer
; clobbers everything volatile plus rbx r12 r13 xmm6-xmm14
solve:
	lea rbx, [scr]
	movdqa xmm8, [k_ch0]
	movdqa xmm9, [k_nl]
	movdqa xmm10, [k_ten]
	pxor xmm11, xmm11
	movdqa xmm12, [k_two]
	movdqa xmm14, [k_adj]
	xor r8d, r8d
	cmp rsi, rdi
	jae .done
align 32
.loop:
	movdqu xmm0, [rsi]
	movdqu xmm1, [rsi + 16]
	movdqu xmm2, [rsi - 1]
	movdqu xmm3, [rsi + 15]
	movdqu xmm4, [rsi - 2]
	movdqu xmm5, [rsi + 14]
	psubusb xmm2, xmm8
	psubusb xmm3, xmm8
	psubusb xmm4, xmm8
	psubusb xmm5, xmm8
	pmullw xmm4, xmm10
	pmullw xmm5, xmm10
	paddb xmm2, xmm4
	paddb xmm3, xmm5
	movdqa [rbx], xmm2
	movdqa [rbx + 16], xmm3
	movdqa xmm6, xmm8
	movdqa xmm7, xmm8
	pcmpgtb xmm6, xmm0
	pcmpgtb xmm7, xmm1
	pcmpeqb xmm0, xmm9
	pcmpeqb xmm1, xmm9
	pmovmskb eax, xmm6
	pmovmskb ecx, xmm7
	pmovmskb edx, xmm0
	pmovmskb r9d, xmm1
	shl ecx, 16
	shl r9d, 16
	or eax, ecx
	or edx, r9d
	tzcnt ecx, edx
	lea rsi, [rsi + rcx + 1]
	mov r9d, eax
	not r9d
	add r9d, r9d
	and eax, r9d
	mov r10d, 2
	shl r10, cl
	dec r10
	and eax, r10d
	popcnt r11d, eax
	tzcnt ecx, eax
	blsr eax, eax
	movzx r12d, byte [rbx + rcx]
%assign k 1
%rep 7
	tzcnt ecx, eax
	blsr eax, eax
	movzx ecx, byte [rbx + rcx]
	shl rcx, 8 * k
	or r12, rcx
%assign k k + 1
%endrep
	cmp r11d, 2
	jb .short
	lea ecx, [r11 - 1]
	mov r13d, 1
	shl r13d, cl
	dec r13d
	movq xmm0, r12
	movdqa xmm1, xmm0
	psrlq xmm1, 8
	psubb xmm1, xmm0
	punpcklqdq xmm1, xmm1
	paddb xmm1, xmm14
	psubusb xmm1, xmm12
	pcmpeqb xmm1, xmm11
	pmovmskb eax, xmm1
	mov edx, eax
	shr edx, 8
	and eax, r13d
	and edx, r13d
	xor eax, r13d
	xor edx, r13d
	cmp eax, 1
	sbb ecx, ecx
	cmp edx, 1
	sbb edx, edx
	or ecx, edx
	sub r8d, ecx
	cmp rsi, rdi
	jb .loop
.done:
	mov eax, r8d
	ret
.short:
	add r8d, r11d
	cmp rsi, rdi
	jb .loop
	jmp .done

%ifdef LINUXTEST
solve_test:
	push rbx
	push rbp
	push r12
	push r13
	push r14
	push r15
	lea rax, [rdi + rsi]
	mov rsi, rdi
	mov rdi, rax
	call solve
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbp
	pop rbx
	ret
%else

; rcx = path -> rax = bytes read into buf+FRONT, or -1
openread:
	sub rsp, 72
	mov edx, 0x80000000
	mov r8d, 1
	xor r9d, r9d
	mov qword [rsp + 32], 3
	mov qword [rsp + 40], 0x80
	mov qword [rsp + 48], 0
	call [__imp_CreateFileA]
	cmp rax, -1
	je .fail
	mov rcx, rax
	lea rdx, [buf + FRONT]
	mov r8d, BUFSZ
	lea r9, [gotn]
	mov qword [rsp + 32], 0
	call [__imp_ReadFile]
	mov eax, [gotn]
	add rsp, 72
	ret
.fail:
	mov rax, -1
	add rsp, 72
	ret

start:
	sub rsp, 72
	call [__imp_GetCommandLineA]
	mov rsi, rax
	cmp byte [rsi], '"'
	jne .unq
	inc rsi
.q:
	mov al, [rsi]
	test al, al
	jz .noarg
	inc rsi
	cmp al, '"'
	jne .q
	jmp .sk
.unq:
	mov al, [rsi]
	test al, al
	jz .noarg
	cmp al, ' '
	je .sk
	inc rsi
	jmp .unq
.sk:
	mov al, [rsi]
	cmp al, ' '
	je .sk1
	cmp al, 9
	jne .sk2
.sk1:
	inc rsi
	jmp .sk
.sk2:
	test al, al
	jz .noarg
	cmp al, '"'
	jne .a1
	inc rsi
	mov al, [rsi]
.a1:
	lea ecx, [rax - '0']
	cmp cl, 9
	ja .path
	lea rdi, [buf + FRONT]
.cd:
	mov al, [rsi]
	test al, al
	jz .cdone
	cmp al, '"'
	je .cdone
	mov [rdi], al
	inc rsi
	inc rdi
	jmp .cd
.cdone:
	lea rax, [buf + FRONT]
	sub rdi, rax
	mov rax, rdi
	jmp .have
.path:
	lea rdi, [pathb]
.cp:
	mov al, [rsi]
	test al, al
	jz .cpd
	cmp al, '"'
	je .cpd
	mov [rdi], al
	inc rsi
	inc rdi
	jmp .cp
.cpd:
	dec rdi
	cmp byte [rdi], ' '
	je .cpd
	mov byte [rdi + 1], 0
	lea rcx, [pathb]
	call openread
	jmp .have
.noarg:
	lea rcx, [p_0]
	call openread
	test rax, rax
	jns .have
	lea rcx, [p_1]
	call openread
	test rax, rax
	jns .have
	lea rcx, [p_2]
	call openread
	test rax, rax
	jns .have
	lea rcx, [p_3]
	call openread
.have:
	test rax, rax
	js .bad
	lea rsi, [buf + FRONT]
	lea rdi, [rsi + rax]
	mov byte [rdi], 10
	call solve
	lea rdi, [outb + 32]
	dec rdi
	mov byte [rdi], 10
	mov ecx, 10
.dg:
	xor edx, edx
	div ecx
	add dl, '0'
	dec rdi
	mov [rdi], dl
	test eax, eax
	jnz .dg
	sub rdi, 8
	mov rax, "Part 1: "
	mov [rdi], rax
	mov r12, rdi
	mov ecx, -11
	call [__imp_GetStdHandle]
	mov rcx, rax
	mov rdx, r12
	lea r8, [outb + 32]
	sub r8, r12
	lea r9, [gotn]
	mov qword [rsp + 32], 0
	call [__imp_WriteFile]
	xor ecx, ecx
	call [__imp_ExitProcess]
.bad:
	mov ecx, 1
	call [__imp_ExitProcess]
%endif