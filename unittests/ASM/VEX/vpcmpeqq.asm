%ifdef CONFIG
{
  "HostFeatures": ["AVX"],
  "RegData": {
    "XMM0": ["0xFFFFFFFFFFFFFFFF", "0x41424344FFFFFFFF", "0x71727374FFFFFFFF", "0xFFFFFFFFFFFFFFFF"],
    "XMM1": ["0xFFFFFFFFFFFFFFFF", "0x51525354FFFFFFFF", "0x61626364FFFFFFFF", "0xFFFFFFFFFFFFFFFF"],
    "XMM2": ["0xFFFFFFFFFFFFFFFF", "0x0000000000000000", "0x0000000000000000", "0xFFFFFFFFFFFFFFFF"],
    "XMM3": ["0xFFFFFFFFFFFFFFFF", "0x0000000000000000", "0x0000000000000000", "0x0000000000000000"],
    "XMM4": ["0xFFFFFFFFFFFFFFFF", "0x0000000000000000", "0x0000000000000000", "0xFFFFFFFFFFFFFFFF"],
    "XMM5": ["0xFFFFFFFFFFFFFFFF", "0x0000000000000000", "0x0000000000000000", "0x0000000000000000"]
  },
  "MemoryRegions": {
    "0x100000000": "4096"
  }
}
%endif

lea rdx, [rel .data]

vmovapd ymm0, [rdx + 32 * 0]
vmovapd ymm1, [rdx + 32 * 1]

; Register only
vpcmpeqq ymm2, ymm0, ymm1
vpcmpeqq xmm3, xmm0, xmm1

; Memory operand
vpcmpeqq ymm4, ymm0, [rdx + 32 * 1]
vpcmpeqq xmm5, xmm0, [rdx + 32 * 1]

hlt

align 32
.data:
dq 0xFFFFFFFFFFFFFFFF
dq 0x41424344FFFFFFFF
dq 0x71727374FFFFFFFF
dq 0xFFFFFFFFFFFFFFFF

dq 0xFFFFFFFFFFFFFFFF
dq 0x51525354FFFFFFFF
dq 0x61626364FFFFFFFF
dq 0xFFFFFFFFFFFFFFFF
