%ifdef CONFIG
{
  "HostFeatures": ["AVX"],
  "RegData": {
    "XMM0": ["0x4142434485868788", "0x5152535455565758", "0x4142434485868788", "0x5152535455565758"],
    "XMM1": ["0x0085008600870088", "0x0041004200430044", "0x0000000000000000", "0x0000000000000000"],
    "XMM2": ["0x0085008600870088", "0x0041004200430044", "0x0055005600570058", "0x0051005200530054"],
    "XMM3": ["0x0085008600870088", "0x0041004200430044", "0x0000000000000000", "0x0000000000000000"],
    "XMM4": ["0x0085008600870088", "0x0041004200430044", "0x0055005600570058", "0x0051005200530054"]
  }
}
%endif

lea rdx, [rel .data]

vmovapd ymm0, [rdx]

; Memory operands
vpmovzxbw xmm1, [rdx]
vpmovzxbw ymm2, [rdx]

; Register only
vpmovzxbw xmm3, xmm0
vpmovzxbw ymm4, xmm0

hlt

align 32
.data:
dq 0x4142434485868788
dq 0x5152535455565758
dq 0x4142434485868788
dq 0x5152535455565758
