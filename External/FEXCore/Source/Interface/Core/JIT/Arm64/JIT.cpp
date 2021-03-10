#include "Interface/Context/Context.h"

#include "Interface/Core/ArchHelpers/Arm64.h"
#include "Interface/Core/ArchHelpers/MContext.h"
#include "Interface/Core/JIT/Arm64/JITClass.h"
#include "Interface/Core/InternalThreadState.h"

#include "Interface/IR/Passes/RegisterAllocationPass.h"

#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Core/UContext.h>
#include "Interface/Core/Interpreter/InterpreterOps.h"

#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

namespace FEXCore::CPU {

void Arm64JITCore::CopyNecessaryDataForCompileThread(CPUBackend *Original) {
  Arm64JITCore *Core = reinterpret_cast<Arm64JITCore*>(Original);
  ThreadSharedData = Core->ThreadSharedData;
}

static void SleepThread(FEXCore::Context::Context *ctx, FEXCore::Core::CpuStateFrame *Frame) {
  auto Thread = Frame->Thread;

  --ctx->IdleWaitRefCount;
  ctx->IdleWaitCV.notify_all();

  // Go to sleep
  Thread->StartRunning.Wait();

  Thread->RunningEvents.Running = true;
  ++ctx->IdleWaitRefCount;
  ctx->IdleWaitCV.notify_all();
}

using namespace vixl;
using namespace vixl::aarch64;

void Arm64JITCore::Op_Unhandled(FEXCore::IR::IROp_Header *IROp, uint32_t Node) {
  FallbackInfo Info;
  if (!InterpreterOps::GetFallbackHandler(IROp, &Info)) {
    auto Name = FEXCore::IR::GetName(IROp->Op);
    LogMan::Msg::A("Unhandled IR Op: %s", std::string(Name).c_str());
  } else {
    switch(Info.ABI) {
      case FABI_VOID_U16:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        mov(w0, GetReg<RA_32>(IROp->Args[0].ID()));
        LoadConstant(x1, (uintptr_t)Info.fn);

        blr(x1);

        PopDynamicRegsAndLR();

        FillStaticRegs();
      }
      break;

      case FABI_F80_F32:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        fmov(v0.S(), GetSrc(IROp->Args[0].ID()).S()) ;
        LoadConstant(x0, (uintptr_t)Info.fn);

        blr(x0);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        eor(GetDst(Node).V16B(), GetDst(Node).V16B(), GetDst(Node).V16B());
        ins(GetDst(Node).V2D(), 0, x0);
        ins(GetDst(Node).V8H(), 4, w1);
      }
      break;

      case FABI_F80_F64:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        mov(v0.D(), GetSrc(IROp->Args[0].ID()).D());
        LoadConstant(x0, (uintptr_t)Info.fn);

        blr(x0);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        eor(GetDst(Node).V16B(), GetDst(Node).V16B(), GetDst(Node).V16B());
        ins(GetDst(Node).V2D(), 0, x0);
        ins(GetDst(Node).V8H(), 4, w1);
      }
      break;

      case FABI_F80_I16:
      case FABI_F80_I32: {
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        mov(w0, GetReg<RA_32>(IROp->Args[0].ID()));
        LoadConstant(x1, (uintptr_t)Info.fn);

        blr(x1);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        eor(GetDst(Node).V16B(), GetDst(Node).V16B(), GetDst(Node).V16B());
        ins(GetDst(Node).V2D(), 0, x0);
        ins(GetDst(Node).V8H(), 4, w1);
      }
      break;

      case FABI_F32_F80:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        umov(x0, GetSrc(IROp->Args[0].ID()).V2D(), 0);
        umov(x1, GetSrc(IROp->Args[0].ID()).V2D(), 1);

        LoadConstant(x2, (uintptr_t)Info.fn);

        blr(x2);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        fmov(GetDst(Node).S(), v0.S());
      }
      break;

      case FABI_F64_F80:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        umov(x0, GetSrc(IROp->Args[0].ID()).V2D(), 0);
        umov(x1, GetSrc(IROp->Args[0].ID()).V2D(), 1);

        LoadConstant(x2, (uintptr_t)Info.fn);

        blr(x2);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        mov(GetDst(Node).D(), v0.D());
      }
      break;

      case FABI_I16_F80:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        umov(x0, GetSrc(IROp->Args[0].ID()).V2D(), 0);
        umov(x1, GetSrc(IROp->Args[0].ID()).V2D(), 1);

        LoadConstant(x2, (uintptr_t)Info.fn);

        blr(x2);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        uxth(GetReg<RA_64>(Node), x0);
      }
      break;
      case FABI_I32_F80:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        umov(x0, GetSrc(IROp->Args[0].ID()).V2D(), 0);
        umov(x1, GetSrc(IROp->Args[0].ID()).V2D(), 1);

        LoadConstant(x2, (uintptr_t)Info.fn);

        blr(x2);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        mov(GetReg<RA_32>(Node), w0);
      }
      break;
      case FABI_I64_F80:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        umov(x0, GetSrc(IROp->Args[0].ID()).V2D(), 0);
        umov(x1, GetSrc(IROp->Args[0].ID()).V2D(), 1);

        LoadConstant(x2, (uintptr_t)Info.fn);

        blr(x2);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        mov(GetReg<RA_64>(Node), x0);
      }
      break;
      case FABI_I64_F80_F80:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        umov(x0, GetSrc(IROp->Args[0].ID()).V2D(), 0);
        umov(x1, GetSrc(IROp->Args[0].ID()).V2D(), 1);

        umov(x2, GetSrc(IROp->Args[1].ID()).V2D(), 0);
        umov(x3, GetSrc(IROp->Args[1].ID()).V2D(), 1);

        LoadConstant(x4, (uintptr_t)Info.fn);

        blr(x4);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        mov(GetReg<RA_64>(Node), x0);
      }
      break;
      case FABI_F80_F80:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        umov(x0, GetSrc(IROp->Args[0].ID()).V2D(), 0);
        umov(x1, GetSrc(IROp->Args[0].ID()).V2D(), 1);

        LoadConstant(x2, (uintptr_t)Info.fn);

        blr(x2);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        eor(GetDst(Node).V16B(), GetDst(Node).V16B(), GetDst(Node).V16B());
        ins(GetDst(Node).V2D(), 0, x0);
        ins(GetDst(Node).V8H(), 4, w1);
      }
      break;
      case FABI_F80_F80_F80:{
        SpillStaticRegs();

        PushDynamicRegsAndLR();

        umov(x0, GetSrc(IROp->Args[0].ID()).V2D(), 0);
        umov(x1, GetSrc(IROp->Args[0].ID()).V2D(), 1);

        umov(x2, GetSrc(IROp->Args[1].ID()).V2D(), 0);
        umov(x3, GetSrc(IROp->Args[1].ID()).V2D(), 1);

        LoadConstant(x4, (uintptr_t)Info.fn);

        blr(x4);

        PopDynamicRegsAndLR();

        FillStaticRegs();

        eor(GetDst(Node).V16B(), GetDst(Node).V16B(), GetDst(Node).V16B());
        ins(GetDst(Node).V2D(), 0, x0);
        ins(GetDst(Node).V8H(), 4, w1);
      }
      break;

      case FABI_UNKNOWN:
      default:
      auto Name = FEXCore::IR::GetName(IROp->Op);
        LogMan::Msg::A("Unhandled IR Fallback abi: %s %d", std::string(Name).c_str(), Info.ABI);
    }
  }
}

void Arm64JITCore::Op_NoOp(FEXCore::IR::IROp_Header *IROp, uint32_t Node) {
}

bool Arm64JITCore::IsAddressInJITCode(uint64_t Address, bool IncludeDispatcher) {
  // Check the initial code buffer first
  // It's the most likely place to end up

  uint64_t CodeBase = reinterpret_cast<uint64_t>(InitialCodeBuffer.Ptr);
  uint64_t CodeEnd = CodeBase + InitialCodeBuffer.Size;
  if (Address >= CodeBase &&
      Address < CodeEnd) {
    return true;
  }

  // Check the generated code buffers
  // Not likely to have any but can happen with recursive signals
  for (auto &CodeBuffer : CodeBuffers) {
    CodeBase = reinterpret_cast<uint64_t>(CodeBuffer.Ptr);
    CodeEnd = CodeBase + CodeBuffer.Size;
    if (Address >= CodeBase &&
        Address < CodeEnd) {
      return true;
    }
  }

  if (IncludeDispatcher) {
    // Check the dispatcher. Unlikely to crash here but not impossible
    CodeBase = reinterpret_cast<uint64_t>(DispatcherCodeBuffer.Ptr);
    CodeEnd = CodeBase + DispatcherCodeBuffer.Size;
    if (Address >= CodeBase &&
        Address < CodeEnd) {
      return true;
    }
  }
  return false;
}

void Arm64JITCore::StoreThreadState(int Signal, void *ucontext) {

  // We can end up getting a signal at any point in our host state
  // Jump to a handler that saves all state so we can safely return
  uint64_t OldSP = ArchHelpers::Context::GetSp(ucontext);
  uintptr_t NewSP = OldSP;

  size_t StackOffset = sizeof(ArmContextBackup);
  NewSP -= StackOffset;
  NewSP = AlignDown(NewSP, 16);

  ArmContextBackup *Context = reinterpret_cast<ArmContextBackup*>(NewSP);
  ArchHelpers::Context::BackupContext(ucontext, Context);

  // Retain the action pointer so we can see it when we return
  Context->Signal = Signal;

  // Save guest state
  // We can't guarantee if registers are in context or host GPRs
  // So we need to save everything
  memcpy(&Context->GuestState, ThreadState->CurrentFrame, sizeof(FEXCore::Core::CPUState));

  // Set the new SP
  ArchHelpers::Context::SetSp(ucontext, NewSP);
}

void Arm64JITCore::RestoreThreadState(void *ucontext) {
  uint64_t OldSP = ArchHelpers::Context::GetSp(ucontext);
  uintptr_t NewSP = OldSP;
  ArmContextBackup *Context = reinterpret_cast<ArmContextBackup*>(NewSP);

  // First thing, reset the guest state
  memcpy(ThreadState->CurrentFrame, &Context->GuestState, sizeof(FEXCore::Core::CPUState));

  ArchHelpers::Context::RestoreContext(ucontext, Context);

  // Restore the previous signal state
  // This allows recursive signals to properly handle signal masking as we are walking back up the list of signals
  CTX->SignalDelegation->SetCurrentSignal(Context->Signal);
}

bool Arm64JITCore::HandleSIGILL(int Signal, void *info, void *ucontext) {

  if (ArchHelpers::Context::GetPc(ucontext) == ThreadSharedData.SignalReturnInstruction) {
    RestoreThreadState(ucontext);

    // Ref count our faults
    // We use this to track if it is safe to clear cache
    --SignalHandlerRefCounter;
    return true;
  }

  if (ArchHelpers::Context::GetPc(ucontext) == PauseReturnInstruction) {
    RestoreThreadState(ucontext);

    // Ref count our faults
    // We use this to track if it is safe to clear cache
    --SignalHandlerRefCounter;
    return true;
  }

  return false;
}

bool Arm64JITCore::HandleGuestSignal(int Signal, void *info, void *ucontext, GuestSigAction *GuestAction, stack_t *GuestStack) {
  StoreThreadState(Signal, ucontext);

  auto Frame = ThreadState->CurrentFrame;

  // Set the new PC
  ArchHelpers::Context::SetPc(ucontext, AbsoluteLoopTopAddressFillSRA);
  // Set x28 (which is our state register) to point to our guest thread data
  ArchHelpers::Context::SetState(ucontext, reinterpret_cast<uint64_t>(Frame));

  // Ref count our faults
  // We use this to track if it is safe to clear cache
  ++SignalHandlerRefCounter;

  ThreadState->CurrentFrame->State.gregs[X86State::REG_RDI] = Signal;
  uint64_t OldGuestSP = ThreadState->CurrentFrame->State.gregs[X86State::REG_RSP];
  uint64_t NewGuestSP = OldGuestSP;

  if (!(GuestStack->ss_flags & SS_DISABLE)) {
    // If our guest is already inside of the alternative stack
    // Then that means we are hitting recursive signals and we need to walk back the stack correctly
    uint64_t AltStackBase = reinterpret_cast<uint64_t>(GuestStack->ss_sp);
    uint64_t AltStackEnd = AltStackBase + GuestStack->ss_size;
    if (OldGuestSP >= AltStackBase &&
        OldGuestSP <= AltStackEnd) {
      // We are already in the alt stack, the rest of the code will handle adjusting this
    }
    else {
      NewGuestSP = AltStackEnd;
    }
  }

  // Back up past the redzone, which is 128bytes
  // Don't need this offset if we aren't going to be putting siginfo in to it
  NewGuestSP -= 128;

  if (GuestAction->sa_flags & SA_SIGINFO) {
     if (!IsAddressInJITCode(ArchHelpers::Context::GetPc(ucontext), false)) {
      // We are in non-jit, SRA is already spilled
      LogMan::Throw::A(!IsAddressInJITCode(ArchHelpers::Context::GetPc(ucontext), true), "Signals in dispatcher have unsynchronized context");
    } else {
      // We are in jit, SRA must be spilled
      for(int i = 0; i < SRA64.size(); i++) {
        ThreadState->CurrentFrame->State.gregs[i] = ArchHelpers::Context::GetArmReg(ucontext, SRA64[i].GetCode());
      }
      // TODO: Also recover FPRs, not sure where the neon context is
      // This is usually not needed
      /*
      for(int i = 0; i < SRAFPR.size(); i++) {
        State->State.State.xmm[i][0] = _mcontext.neon[SRAFPR[i].GetCode()];
        State->State.State.xmm[i][0] = _mcontext.neon[SRAFPR[i].GetCode()];
      }
      */
    }

    // Setup ucontext a bit
    if (CTX->Config.Is64BitMode) {
      NewGuestSP -= sizeof(FEXCore::x86_64::ucontext_t);
      uint64_t UContextLocation = NewGuestSP;

      FEXCore::x86_64::ucontext_t *guest_uctx = reinterpret_cast<FEXCore::x86_64::ucontext_t*>(UContextLocation);

      // We have extended float information
      guest_uctx->uc_flags |= FEXCore::x86_64::UC_FP_XSTATE;

      // Pointer to where the fpreg memory is
      guest_uctx->uc_mcontext.fpregs = &guest_uctx->__fpregs_mem;

#define COPY_REG(x) \
      guest_uctx->uc_mcontext.gregs[FEXCore::x86_64::FEX_REG_##x] = Frame->State.gregs[X86State::REG_##x];
      COPY_REG(R8);
      COPY_REG(R9);
      COPY_REG(R10);
      COPY_REG(R11);
      COPY_REG(R12);
      COPY_REG(R13);
      COPY_REG(R14);
      COPY_REG(R15);
      COPY_REG(RDI);
      COPY_REG(RSI);
      COPY_REG(RBP);
      COPY_REG(RBX);
      COPY_REG(RDX);
      COPY_REG(RAX);
      COPY_REG(RCX);
      COPY_REG(RSP);
#undef COPY_REG

      // Copy float registers
      memcpy(guest_uctx->__fpregs_mem._st, Frame->State.mm, sizeof(Frame->State.mm));
      memcpy(guest_uctx->__fpregs_mem._xmm, Frame->State.xmm, sizeof(Frame->State.xmm));

      // FCW store default
      guest_uctx->__fpregs_mem.fcw = Frame->State.FCW;

      // Reconstruct FSW
      guest_uctx->__fpregs_mem.fsw =
        (Frame->State.flags[FEXCore::X86State::X87FLAG_TOP_LOC] << 11) |
        (Frame->State.flags[FEXCore::X86State::X87FLAG_C0_LOC] << 8) |
        (Frame->State.flags[FEXCore::X86State::X87FLAG_C1_LOC] << 9) |
        (Frame->State.flags[FEXCore::X86State::X87FLAG_C2_LOC] << 10) |
        (Frame->State.flags[FEXCore::X86State::X87FLAG_C3_LOC] << 14);

      // Copy over signal stack information
      guest_uctx->uc_stack.ss_flags = GuestStack->ss_flags;
      guest_uctx->uc_stack.ss_sp = GuestStack->ss_sp;
      guest_uctx->uc_stack.ss_size = GuestStack->ss_size;

      // XXX: siginfo_t(RSI)
      Frame->State.gregs[X86State::REG_RSI] = 0x4142434445460000;
      Frame->State.gregs[X86State::REG_RDX] = UContextLocation;
    }
    else {
      // XXX: 32bit Support
      NewGuestSP -= sizeof(FEXCore::x86::ucontext_t);
      uint64_t UContextLocation = 0; // NewGuestSP;
      NewGuestSP -= sizeof(FEXCore::x86::siginfo_t);
      uint64_t SigInfoLocation = 0; // NewGuestSP;

      NewGuestSP -= 4;
      *(uint32_t*)NewGuestSP = UContextLocation;
      NewGuestSP -= 4;
      *(uint32_t*)NewGuestSP = SigInfoLocation;
    }

    Frame->State.rip = reinterpret_cast<uint64_t>(GuestAction->sigaction_handler.sigaction);
  }
  else {
    Frame->State.rip = reinterpret_cast<uint64_t>(GuestAction->sigaction_handler.handler);
  }

  if (CTX->Config.Is64BitMode) {
    Frame->State.gregs[X86State::REG_RDI] = Signal;

    // Set up the new SP for stack handling
    NewGuestSP -= 8;
    *(uint64_t*)NewGuestSP = CTX->X86CodeGen.SignalReturn;
    Frame->State.gregs[X86State::REG_RSP] = NewGuestSP;
  }
  else {
    NewGuestSP -= 4;
    *(uint32_t*)NewGuestSP = CTX->X86CodeGen.SignalReturn;
    LogMan::Throw::A(CTX->X86CodeGen.SignalReturn < 0x1'0000'0000ULL, "This needs to be below 4GB");
    Frame->State.gregs[X86State::REG_RSP] = NewGuestSP;
  }

  return true;
}

Arm64JITCore::CodeBuffer Arm64JITCore::AllocateNewCodeBuffer(size_t Size) {
  CodeBuffer Buffer;
  Buffer.Size = Size;
  Buffer.Ptr = static_cast<uint8_t*>(
               mmap(nullptr,
                    Buffer.Size,
                    PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS,
                    -1, 0));
  LogMan::Throw::A(!!Buffer.Ptr, "Couldn't allocate code buffer");
  return Buffer;
}

void Arm64JITCore::FreeCodeBuffer(CodeBuffer Buffer) {
  munmap(Buffer.Ptr, Buffer.Size);
}

bool Arm64JITCore::HandleSIGBUS(int Signal, void *info, void *ucontext) {

  uint32_t *PC = (uint32_t*)ArchHelpers::Context::GetPc(ucontext);
  uint32_t Instr = PC[0];

  if (!IsAddressInJITCode(ArchHelpers::Context::GetPc(ucontext))) {
    // Wasn't a sigbus in JIT code
    return false;
  }

  // 1 = 16bit
  // 2 = 32bit
  // 3 = 64bit
  uint32_t Size = (Instr & 0xC000'0000) >> 30;
  uint32_t AddrReg = (Instr >> 5) & 0x1F;
  uint32_t DataReg = Instr & 0x1F;
  uint32_t DMB = 0b1101'0101'0000'0011'0011'0000'1011'1111 |
    0b1011'0000'0000; // Inner shareable all
  if ((Instr & 0x3F'FF'FC'00) == 0x08'DF'FC'00 || // LDAR*
      (Instr & 0x3F'FF'FC'00) == 0x38'BF'C0'00) { // LDAPR*
    uint32_t LDR = 0b0011'1000'0111'1111'0110'1000'0000'0000;
    LDR |= Size << 30;
    LDR |= AddrReg << 5;
    LDR |= DataReg;
    PC[-1] = DMB;
    PC[0] = LDR;
    PC[1] = DMB;
    // Back up one instruction and have another go
    ArchHelpers::Context::SetPc(ucontext, ArchHelpers::Context::GetPc(ucontext) - 4);
  }
  else if ( (Instr & 0x3F'FF'FC'00) == 0x08'9F'FC'00) { // STLR*
    uint32_t STR = 0b0011'1000'0011'1111'0110'1000'0000'0000;
    STR |= Size << 30;
    STR |= AddrReg << 5;
    STR |= DataReg;
    PC[-1] = DMB;
    PC[0] = STR;
    PC[1] = DMB;
    // Back up one instruction and have another go
    ArchHelpers::Context::SetPc(ucontext, ArchHelpers::Context::GetPc(ucontext) - 4);
  }
  else if ((Instr & FEXCore::ArchHelpers::Arm64::CASPAL_MASK) == FEXCore::ArchHelpers::Arm64::CASPAL_INST) { // CASPAL
    if (FEXCore::ArchHelpers::Arm64::HandleCASPAL(ucontext, info, Instr)) {
      // Skip this instruction now
      ArchHelpers::Context::SetPc(ucontext, ArchHelpers::Context::GetPc(ucontext) + 4);
      return true;
    }
    else {
      LogMan::Msg::E("Unhandled JIT SIGBUS CASPAL: PC: %p Instruction: 0x%08x\n", PC, PC[0]);
      return false;
    }
  }
  else if ((Instr & FEXCore::ArchHelpers::Arm64::CASAL_MASK) == FEXCore::ArchHelpers::Arm64::CASAL_INST) { // CASAL
    if (FEXCore::ArchHelpers::Arm64::HandleCASAL(ucontext, info, Instr)) {
      // Skip this instruction now
      ArchHelpers::Context::SetPc(ucontext, ArchHelpers::Context::GetPc(ucontext) + 4);
      return true;
    }
    else {
      LogMan::Msg::E("Unhandled JIT SIGBUS CASAL: PC: %p Instruction: 0x%08x\n", PC, PC[0]);
      return false;
    }
  }
  else if ((Instr & FEXCore::ArchHelpers::Arm64::ATOMIC_MEM_MASK) == FEXCore::ArchHelpers::Arm64::ATOMIC_MEM_INST) { // Atomic memory op
    if (FEXCore::ArchHelpers::Arm64::HandleAtomicMemOp(ucontext, info, Instr)) {
      // Skip this instruction now
      ArchHelpers::Context::SetPc(ucontext, ArchHelpers::Context::GetPc(ucontext) + 4);
      return true;
    }
    else {
      uint8_t Op = (PC[0] >> 12) & 0xF;
      LogMan::Msg::E("Unhandled JIT SIGBUS Atomic mem op 0x%02x: PC: %p Instruction: 0x%08x\n", Op, PC, PC[0]);
      return false;
    }
  }
  else {
    LogMan::Msg::E("Unhandled JIT SIGBUS: PC: %p Instruction: 0x%08x\n", PC, PC[0]);
    return false;
  }

  vixl::aarch64::CPU::EnsureIAndDCacheCoherency(&PC[-1], 16);
  return true;
}

bool Arm64JITCore::HandleSignalPause(int Signal, void *info, void *ucontext) {
  FEXCore::Core::SignalEvent SignalReason = ThreadState->SignalReason.load();
  auto Frame = ThreadState->CurrentFrame;

  if (SignalReason == FEXCore::Core::SignalEvent::SIGNALEVENT_PAUSE) {
    // Store our thread state so we can come back to this
    StoreThreadState(Signal, ucontext);

    if (!IsAddressInJITCode(ArchHelpers::Context::GetPc(ucontext), false)) {
      // We are in non-jit, SRA is already spilled
      LogMan::Throw::A(!IsAddressInJITCode(ArchHelpers::Context::GetPc(ucontext), true), "Signals in dispatcher have unsynchronized context");
      ArchHelpers::Context::SetPc(ucontext, ThreadPauseHandlerAddress);
    } else {
      // We are in jit, SRA must be spilled
      ArchHelpers::Context::SetPc(ucontext, ThreadPauseHandlerAddressSpillSRA);
    }

    // Set x28 (which is our state register) to point to our guest thread data
    ArchHelpers::Context::SetState(ucontext, reinterpret_cast<uint64_t>(Frame));
    // Ref count our faults
    // We use this to track if it is safe to clear cache
    ++SignalHandlerRefCounter;

    ThreadState->SignalReason.store(FEXCore::Core::SIGNALEVENT_NONE);
    return true;
  }

  if (SignalReason == FEXCore::Core::SignalEvent::SIGNALEVENT_RETURN) {
    RestoreThreadState(ucontext);

    // Ref count our faults
    // We use this to track if it is safe to clear cache
    --SignalHandlerRefCounter;

    ThreadState->SignalReason.store(FEXCore::Core::SIGNALEVENT_NONE);
    return true;
  }

  if (SignalReason == FEXCore::Core::SignalEvent::SIGNALEVENT_STOP) {
    // Our thread is stopping
    // We don't care about anything at this point
    // Set the stack to our starting location when we entered the JIT and get out safely
    ArchHelpers::Context::SetSp(ucontext, Frame->ReturningStackLocation);

    // Our ref counting doesn't matter anymore
    SignalHandlerRefCounter = 0;

    // Set the new PC
    if (!IsAddressInJITCode(ArchHelpers::Context::GetPc(ucontext), false)) {
      // We are in non-jit, SRA is already spilled
      LogMan::Throw::A(!IsAddressInJITCode(ArchHelpers::Context::GetPc(ucontext), true), "Signals in dispatcher have unsynchronized context");
      ArchHelpers::Context::SetPc(ucontext, ThreadStopHandlerAddress);
    } else {
      // We are in jit, SRA must be spilled
      ArchHelpers::Context::SetPc(ucontext, ThreadStopHandlerAddressSpillSRA);
    }

    // Set x28 (which is our state register) to point to our guest thread data
    ArchHelpers::Context::SetState(ucontext, reinterpret_cast<uint64_t>(Frame));

    ThreadState->SignalReason.store(FEXCore::Core::SIGNALEVENT_NONE);
    return true;
  }

  return false;
}

Arm64JITCore::Arm64JITCore(FEXCore::Context::Context *ctx, FEXCore::Core::InternalThreadState *Thread, CodeBuffer Buffer, bool CompileThread)
  : vixl::aarch64::Assembler(Buffer.Ptr, Buffer.Size, vixl::aarch64::PositionDependentCode)
  , CTX {ctx}
  , ThreadState {Thread}
  , InitialCodeBuffer {Buffer}
{
  CurrentCodeBuffer = &InitialCodeBuffer;
  ThreadSharedData.SignalHandlerRefCounterPtr = &SignalHandlerRefCounter;

  auto Features = vixl::CPUFeatures::InferFromOS();
  SupportsAtomics = Features.Has(vixl::CPUFeatures::Feature::kAtomics);
  // RCPC is bugged on Snapdragon 865
  // Causes glibc cond16 test to immediately throw assert
  // __pthread_mutex_cond_lock: Assertion `mutex->__data.__owner == 0'
  SupportsRCPC = false; //Features.Has(vixl::CPUFeatures::Feature::kRCpc);

  if (SupportsAtomics) {
    // Hypervisor can hide this on the c630?
    Features.Combine(vixl::CPUFeatures::Feature::kLORegions);
  }

  SetCPUFeatures(Features);

  if (!SupportsAtomics) {
    WARN_ONCE("Host CPU doesn't support atomics. Expect bad performance");
  }

  RAPass = Thread->PassManager->GetRAPass();

#if DEBUG
  Decoder.AppendVisitor(&Disasm)
#endif
  CPU.SetUp();
  SetAllowAssembler(true);

  uint32_t NumUsedGPRs = NumGPRs;
  uint32_t NumUsedGPRPairs = NumGPRPairs;
  uint32_t UsedRegisterCount = RegisterCount;

  RAPass->AllocateRegisterSet(UsedRegisterCount, RegisterClasses);

  RAPass->AddRegisters(FEXCore::IR::GPRClass, NumUsedGPRs);
  RAPass->AddRegisters(FEXCore::IR::GPRFixedClass, SRA64.size());
  RAPass->AddRegisters(FEXCore::IR::FPRClass, NumFPRs);
  RAPass->AddRegisters(FEXCore::IR::FPRFixedClass, SRAFPR.size()  );
  RAPass->AddRegisters(FEXCore::IR::GPRPairClass, NumUsedGPRPairs);
  RAPass->AddRegisters(FEXCore::IR::ComplexClass, 1);

  for (uint32_t i = 0; i < NumUsedGPRPairs; ++i) {
    RAPass->AddRegisterConflict(FEXCore::IR::GPRClass, i * 2,     FEXCore::IR::GPRPairClass, i);
    RAPass->AddRegisterConflict(FEXCore::IR::GPRClass, i * 2 + 1, FEXCore::IR::GPRPairClass, i);
  }

  for (uint32_t i = 0; i < FEXCore::IR::IROps::OP_LAST + 1; ++i) {
    OpHandlers[i] = &Arm64JITCore::Op_Unhandled;
  }

  RegisterALUHandlers();
  RegisterAtomicHandlers();
  RegisterBranchHandlers();
  RegisterConversionHandlers();
  RegisterFlagHandlers();
  RegisterMemoryHandlers();
  RegisterMiscHandlers();
  RegisterMoveHandlers();
  RegisterVectorHandlers();
  RegisterEncryptionHandlers();

  if (!CompileThread) {
    CreateCustomDispatch(Thread);

    // This will register the host signal handler per thread, which is fine
    CTX->SignalDelegation->RegisterHostSignalHandler(SIGILL, [](FEXCore::Core::InternalThreadState *Thread, int Signal, void *info, void *ucontext) -> bool {
      Arm64JITCore *Core = reinterpret_cast<Arm64JITCore*>(Thread->CPUBackend.get());
      return Core->HandleSIGILL(Signal, info, ucontext);
    });

    CTX->SignalDelegation->RegisterHostSignalHandler(SIGBUS, [](FEXCore::Core::InternalThreadState *Thread, int Signal, void *info, void *ucontext) -> bool {
      Arm64JITCore *Core = reinterpret_cast<Arm64JITCore*>(Thread->CPUBackend.get());
      return Core->HandleSIGBUS(Signal, info, ucontext);
    });

    CTX->SignalDelegation->RegisterHostSignalHandler(SignalDelegator::SIGNAL_FOR_PAUSE, [](FEXCore::Core::InternalThreadState *Thread, int Signal, void *info, void *ucontext) -> bool {
      Arm64JITCore *Core = reinterpret_cast<Arm64JITCore*>(Thread->CPUBackend.get());
      return Core->HandleSignalPause(Signal, info, ucontext);
    });

    auto GuestSignalHandler = [](FEXCore::Core::InternalThreadState *Thread, int Signal, void *info, void *ucontext, GuestSigAction *GuestAction, stack_t *GuestStack) -> bool {
      Arm64JITCore *Core = reinterpret_cast<Arm64JITCore*>(Thread->CPUBackend.get());
      return Core->HandleGuestSignal(Signal, info, ucontext, GuestAction, GuestStack);
    };

    for (uint32_t Signal = 0; Signal < SignalDelegator::MAX_SIGNALS; ++Signal) {
      CTX->SignalDelegation->RegisterHostSignalHandlerForGuest(Signal, GuestSignalHandler);
    }
  }
}

void Arm64JITCore::ClearCache() {
  // Get the backing code buffer
  auto Buffer = GetBuffer();
  if (*ThreadSharedData.SignalHandlerRefCounterPtr == 0) {
    if (!CodeBuffers.empty()) {
      // If we have more than one code buffer we are tracking then walk them and delete
      // This is a cleanup step
      for (auto CodeBuffer : CodeBuffers) {
        FreeCodeBuffer(CodeBuffer);
      }
      CodeBuffers.clear();

      // Set the current code buffer to the initial
      *Buffer = vixl::CodeBuffer(InitialCodeBuffer.Ptr, InitialCodeBuffer.Size);
      CurrentCodeBuffer = &InitialCodeBuffer;
    }

    if (CurrentCodeBuffer->Size == MAX_CODE_SIZE) {
      // Rewind to the start of the code cache start
      Buffer->Reset();
    }
    else {
      FreeCodeBuffer(InitialCodeBuffer);

      // Resize the code buffer and reallocate our code size
      InitialCodeBuffer.Size *= 1.5;
      InitialCodeBuffer.Size = std::min(InitialCodeBuffer.Size, MAX_CODE_SIZE);

      InitialCodeBuffer = Arm64JITCore::AllocateNewCodeBuffer(InitialCodeBuffer.Size);
      *Buffer = vixl::CodeBuffer(InitialCodeBuffer.Ptr, InitialCodeBuffer.Size);
    }
  }
  else {
    // We have signal handlers that have generated code
    // This means that we can not safely clear the code at this point in time
    // Allocate some new code buffers that we can switch over to instead
    auto NewCodeBuffer = Arm64JITCore::AllocateNewCodeBuffer(Arm64JITCore::INITIAL_CODE_SIZE);
    EmplaceNewCodeBuffer(NewCodeBuffer);
    *Buffer = vixl::CodeBuffer(NewCodeBuffer.Ptr, NewCodeBuffer.Size);
  }
}

Arm64JITCore::~Arm64JITCore() {
  for (auto CodeBuffer : CodeBuffers) {
    FreeCodeBuffer(CodeBuffer);
  }
  CodeBuffers.clear();

  if (DispatcherCodeBuffer.Ptr) {
    // Dispatcher may not exist if this is a compile thread
    FreeCodeBuffer(DispatcherCodeBuffer);
  }
  FreeCodeBuffer(InitialCodeBuffer);
}

void Arm64JITCore::LoadConstant(vixl::aarch64::Register Reg, uint64_t Constant) {
  bool Is64Bit = Reg.IsX();
  int Segments = Is64Bit ? 4 : 2;

  if (Is64Bit && ((~Constant)>> 16) == 0) {
    movn(Reg, (~Constant) & 0xFFFF);
    return;
  }

  movz(Reg, (Constant) & 0xFFFF, 0);
  for (int i = 1; i < Segments; ++i) {
    uint16_t Part = (Constant >> (i * 16)) & 0xFFFF;
    if (Part) {
      movk(Reg, Part, i * 16);
    }
  }
}

static IR::PhysicalRegister GetPhys(IR::RegisterAllocationData *RAData, uint32_t Node) {
  auto PhyReg = RAData->GetNodeRegister(Node);

  LogMan::Throw::A(!PhyReg.IsInvalid(), "Couldn't Allocate register for node: ssa%d. Class: %d", Node, PhyReg.Class);

  return PhyReg;
}

template<>
aarch64::Register Arm64JITCore::GetReg<Arm64JITCore::RA_32>(uint32_t Node) {
auto Reg = GetPhys(RAData, Node);
  if (Reg.Class == IR::GPRFixedClass.Val) {
    return SRA64[Reg.Reg].W();
  } else if (Reg.Class == IR::GPRClass.Val) {
    return RA64[Reg.Reg].W();
  } else {
    LogMan::Throw::A(false, "Unexpected Class: %d", Reg.Class);
  }
}

template<>
aarch64::Register Arm64JITCore::GetReg<Arm64JITCore::RA_64>(uint32_t Node) {
  auto Reg = GetPhys(RAData, Node);
  if (Reg.Class == IR::GPRFixedClass.Val) {
    return SRA64[Reg.Reg];
  } else if (Reg.Class == IR::GPRClass.Val) {
    return RA64[Reg.Reg];
  } else {
    LogMan::Throw::A(false, "Unexpected Class: %d", Reg.Class);
  }
}

template<>
std::pair<aarch64::Register, aarch64::Register> Arm64JITCore::GetSrcPair<Arm64JITCore::RA_32>(uint32_t Node) {
  uint32_t Reg = GetPhys(RAData, Node).Reg;
  return RA32Pair[Reg];
}

template<>
std::pair<aarch64::Register, aarch64::Register> Arm64JITCore::GetSrcPair<Arm64JITCore::RA_64>(uint32_t Node) {
  uint32_t Reg = GetPhys(RAData, Node).Reg;
  return RA64Pair[Reg];
}

aarch64::VRegister Arm64JITCore::GetSrc(uint32_t Node) {
  auto Reg = GetPhys(RAData, Node);
  if (Reg.Class == IR::FPRFixedClass.Val) {
    return SRAFPR[Reg.Reg];
  } else if (Reg.Class == IR::FPRClass.Val) {
    return RAFPR[Reg.Reg];
  } else {
    LogMan::Throw::A(false, "Unexpected Class: %d", Reg.Class);
  }
}

aarch64::VRegister Arm64JITCore::GetDst(uint32_t Node) {
  auto Reg = GetPhys(RAData, Node);
  if (Reg.Class == IR::FPRFixedClass.Val) {
    return SRAFPR[Reg.Reg];
  } else if (Reg.Class == IR::FPRClass.Val) {
    return RAFPR[Reg.Reg];
  } else {
    LogMan::Throw::A(false, "Unexpected Class: %d", Reg.Class);
  }
}

bool Arm64JITCore::IsInlineConstant(const IR::OrderedNodeWrapper& WNode, uint64_t* Value) {
  auto OpHeader = IR->GetOp<IR::IROp_Header>(WNode);

  if (OpHeader->Op == IR::IROps::OP_INLINECONSTANT) {
    auto Op = OpHeader->C<IR::IROp_InlineConstant>();
    if (Value) {
      *Value = Op->Constant;
    }
    return true;
  } else {
    return false;
  }
}

bool Arm64JITCore::IsInlineEntrypointOffset(const IR::OrderedNodeWrapper& WNode, uint64_t* Value) {
  auto OpHeader = IR->GetOp<IR::IROp_Header>(WNode);

  if (OpHeader->Op == IR::IROps::OP_INLINEENTRYPOINTOFFSET) {
    auto Op = OpHeader->C<IR::IROp_InlineEntrypointOffset>();
    if (Value) {
      *Value = IR->GetHeader()->Entry + Op->Offset;
    }
    return true;
  } else {
    return false;
  }
}

FEXCore::IR::RegisterClassType Arm64JITCore::GetRegClass(uint32_t Node) {
  return FEXCore::IR::RegisterClassType {GetPhys(RAData, Node).Class};
}


bool Arm64JITCore::IsFPR(uint32_t Node) {
  auto Class = GetRegClass(Node);

  return Class == IR::FPRClass || Class == IR::FPRFixedClass;
}

bool Arm64JITCore::IsGPR(uint32_t Node) {
  auto Class = GetRegClass(Node);

  return Class == IR::GPRClass || Class == IR::GPRFixedClass;
}

void *Arm64JITCore::CompileCode([[maybe_unused]] FEXCore::IR::IRListView const *IR, [[maybe_unused]] FEXCore::Core::DebugData *DebugData, FEXCore::IR::RegisterAllocationData *RAData) {
  using namespace aarch64;
  JumpTargets.clear();
  uint32_t SSACount = IR->GetSSACount();

  this->RAData = RAData;

  auto HeaderOp = IR->GetHeader();

  #ifndef NDEBUG
  LoadConstant(x0, HeaderOp->Entry);
  #endif

  this->IR = IR;

  // Fairly excessive buffer range to make sure we don't overflow
  uint32_t BufferRange = SSACount * 16;
  if ((GetCursorOffset() + BufferRange) > CurrentCodeBuffer->Size) {
    ThreadState->CTX->ClearCodeCache(ThreadState, false);
  }

  // AAPCS64
  // r30      = LR
  // r29      = FP
  // r19..r28 = Callee saved
  // r18      = Platform Register (Matters if we target Windows or iOS)
  // r16..r17 = Inter-procedure scratch
  //  r9..r15 = Temp
  //  r8      = Indirect Result
  //  r0...r7 = Parameter/Results
  //
  //  FPRS:
  //  v8..v15 = (lower 64bits) Callee saved

  // Our allocation:
  // X0 = ThreadState
  // X1 = MemBase
  //
  // X1-X3 = Temp
  // X4-r18 = RA

  auto Buffer = GetBuffer();
  auto Entry = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());

 if (CTX->GetGdbServerStatus()) {
    aarch64::Label RunBlock;

    // If we have a gdb server running then run in a less efficient mode that checks if we need to exit
    // This happens when single stepping

    static_assert(sizeof(CTX->Config.RunningMode) == 4, "This is expected to be size of 4");
    ldr(x0, MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, Thread))); // Get thread
    ldr(x0, MemOperand(x0, offsetof(FEXCore::Core::InternalThreadState, CTX))); // Get Context
    ldr(w0, MemOperand(x0, offsetof(FEXCore::Context::Context, Config.RunningMode)));

    // If the value == 0 then we don't need to stop
    cbz(w0, &RunBlock);
    {
      // Make sure RIP is syncronized to the context
      LoadConstant(x0, HeaderOp->Entry);
      str(x0, MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, State.rip)));

      // Stop the thread
      LoadConstant(x0, ThreadPauseHandlerAddressSpillSRA);
      br(x0);
    }
    bind(&RunBlock);
  }

  //LogMan::Throw::A(RAData->HasFullRA(), "Arm64 JIT only works with RA");

  SpillSlots = RAData->SpillSlots();

  if (SpillSlots) {
    if (IsImmAddSub(SpillSlots * 16)) {
      sub(sp, sp, SpillSlots * 16);
    } else {
      LoadConstant(x0, SpillSlots * 16);
      sub(sp, sp, x0);
    }
  }

  PendingTargetLabel = nullptr;

  for (auto [BlockNode, BlockHeader] : IR->GetBlocks()) {
    using namespace FEXCore::IR;
    auto BlockIROp = BlockHeader->CW<FEXCore::IR::IROp_CodeBlock>();
    LogMan::Throw::A(BlockIROp->Header.Op == IR::OP_CODEBLOCK, "IR type failed to be a code block");

    {
      uint32_t Node = IR->GetID(BlockNode);
      auto IsTarget = JumpTargets.find(Node);
      if (IsTarget == JumpTargets.end()) {
        IsTarget = JumpTargets.try_emplace(Node).first;
      }

      // if there's a pending branch, and it is not fall-through
      if (PendingTargetLabel && PendingTargetLabel != &IsTarget->second)
      {
        b(PendingTargetLabel);
      }
      PendingTargetLabel = nullptr;

      bind(&IsTarget->second);
    }

    if (DebugData) {
      DebugData->Subblocks.push_back({Buffer->GetOffsetAddress<uintptr_t>(GetCursorOffset()), 0, IR->GetID(BlockNode)});
    }

    for (auto [CodeNode, IROp] : IR->GetCode(BlockNode)) {
      uint32_t ID = IR->GetID(CodeNode);

      // Execute handler
      OpHandler Handler = OpHandlers[IROp->Op];
      (this->*Handler)(IROp, ID);
    }

    if (DebugData) {
      DebugData->Subblocks.back().HostCodeSize = Buffer->GetOffsetAddress<uintptr_t>(GetCursorOffset()) - DebugData->Subblocks.back().HostCodeStart;
    }
  }

  // Make sure last branch is generated. It certainly can't be eliminated here.
  if (PendingTargetLabel)
  {
    b(PendingTargetLabel);
  }
  PendingTargetLabel = nullptr;

  FinalizeCode();

  auto CodeEnd = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());
  CPU.EnsureIAndDCacheCoherency(reinterpret_cast<void*>(Entry), CodeEnd - reinterpret_cast<uint64_t>(Entry));

  if (DebugData) {
    DebugData->HostCodeSize = reinterpret_cast<uintptr_t>(CodeEnd) - reinterpret_cast<uintptr_t>(Entry);
  }

  this->IR = nullptr;

  return reinterpret_cast<void*>(Entry);
}

void Arm64JITCore::PushCalleeSavedRegisters() {
  // We need to save pairs of registers
  // We save r19-r30
  MemOperand PairOffset(sp, -16, PreIndex);
  const std::array<std::pair<vixl::aarch64::XRegister, vixl::aarch64::XRegister>, 6> CalleeSaved = {{
    {x19, x20},
    {x21, x22},
    {x23, x24},
    {x25, x26},
    {x27, x28},
    {x29, x30},
  }};

  for (auto &RegPair : CalleeSaved) {
    stp(RegPair.first, RegPair.second, PairOffset);
  }

  // Additionally we need to store the lower 64bits of v8-v15
  // Here's a fun thing, we can use two ST4 instructions to store everything
  // We just need a single sub to sp before that
  const std::array<
    std::tuple<vixl::aarch64::VRegister,
               vixl::aarch64::VRegister,
               vixl::aarch64::VRegister,
               vixl::aarch64::VRegister>, 2> FPRs = {{
    {v8, v9, v10, v11},
    {v12, v13, v14, v15},
  }};

  uint32_t VectorSaveSize = sizeof(uint64_t) * 8;
  sub(sp, sp, VectorSaveSize);
  // SP supporting move
  // We just saved x19 so it is safe
  add(x19, sp, 0);

  MemOperand QuadOffset(x19, 32, PostIndex);
  for (auto &RegQuad : FPRs) {
    st4(std::get<0>(RegQuad).D(),
        std::get<1>(RegQuad).D(),
        std::get<2>(RegQuad).D(),
        std::get<3>(RegQuad).D(),
        0,
        QuadOffset);
  }
}

void Arm64JITCore::PopCalleeSavedRegisters() {
  const std::array<
    std::tuple<vixl::aarch64::VRegister,
               vixl::aarch64::VRegister,
               vixl::aarch64::VRegister,
               vixl::aarch64::VRegister>, 2> FPRs = {{
    {v12, v13, v14, v15},
    {v8, v9, v10, v11},
  }};

  MemOperand QuadOffset(sp, 32, PostIndex);
  for (auto &RegQuad : FPRs) {
    ld4(std::get<0>(RegQuad).D(),
        std::get<1>(RegQuad).D(),
        std::get<2>(RegQuad).D(),
        std::get<3>(RegQuad).D(),
        0,
        QuadOffset);
  }

  MemOperand PairOffset(sp, 16, PostIndex);
  const std::array<std::pair<vixl::aarch64::XRegister, vixl::aarch64::XRegister>, 6> CalleeSaved = {{
    {x29, x30},
    {x27, x28},
    {x25, x26},
    {x23, x24},
    {x21, x22},
    {x19, x20},
  }};

  for (auto &RegPair : CalleeSaved) {
    ldp(RegPair.first, RegPair.second, PairOffset);
  }
}

uint64_t Arm64JITCore::ExitFunctionLink(Arm64JITCore *core, FEXCore::Core::CpuStateFrame *Frame, uint64_t *record) {
  auto Thread = Frame->Thread;
  auto GuestRip = record[1];

  auto HostCode = Thread->LookupCache->FindBlock(GuestRip);

  if (!HostCode) {
    //printf("ExitFunctionLink: Aborting, %lX not in cache\n", GuestRip);
    Frame->State.rip = GuestRip;
    return core->AbsoluteLoopTopAddress;
  }

  uintptr_t branch = (uintptr_t)(record) - 8;
  auto LinkerAddress = core->ExitFunctionLinkerAddress;

  auto offset = HostCode/4 - branch/4;
  if (IsInt26(offset)) {
    // optimal case - can branch directly
    // patch the code
    vixl::aarch64::Assembler emit((uint8_t*)(branch), 24);
    vixl::CodeBufferCheckScope scope(&emit, 24, vixl::CodeBufferCheckScope::kDontReserveBufferSpace, vixl::CodeBufferCheckScope::kNoAssert);
    emit.b(offset);
    emit.FinalizeCode();
    vixl::aarch64::CPU::EnsureIAndDCacheCoherency((void*)branch, 24);

    // Add de-linking handler
    Thread->LookupCache->AddBlockLink(GuestRip, (uintptr_t)record, [branch, LinkerAddress]{
      vixl::aarch64::Assembler emit((uint8_t*)(branch), 24);
      vixl::CodeBufferCheckScope scope(&emit, 24, vixl::CodeBufferCheckScope::kDontReserveBufferSpace, vixl::CodeBufferCheckScope::kNoAssert);
      Literal l_BranchHost{LinkerAddress};
      emit.ldr(x0, &l_BranchHost);
      emit.blr(x0);
      emit.place(&l_BranchHost);
      emit.FinalizeCode();
      vixl::aarch64::CPU::EnsureIAndDCacheCoherency((void*)branch, 24);
    });
  } else {
    // fallback case - do a soft-er link by patching the pointer
    record[0] = HostCode;

    // Add de-linking handler
    Thread->LookupCache->AddBlockLink(GuestRip, (uintptr_t)record, [record, LinkerAddress]{
      record[0] = LinkerAddress;
    });
  }



  return HostCode;
}

void Arm64JITCore::CreateCustomDispatch(FEXCore::Core::InternalThreadState *Thread) {
  auto OriginalBuffer = *GetBuffer();

  // Dispatcher lives outside of traditional space-time
  DispatcherCodeBuffer = Arm64JITCore::AllocateNewCodeBuffer(MAX_DISPATCHER_CODE_SIZE);
  *GetBuffer() = vixl::CodeBuffer(DispatcherCodeBuffer.Ptr, DispatcherCodeBuffer.Size);

  auto Buffer = GetBuffer();

  DispatchPtr = Buffer->GetOffsetAddress<CPUBackend::AsmDispatch>(GetCursorOffset());

  // while (!Thread->State.RunningEvents.ShouldStop.load()) {
  //    Ptr = FindBlock(RIP)
  //    if (!Ptr)
  //      Ptr = CTX->CompileBlock(RIP);
  //
  //    if (Ptr)
  //      Ptr();
  //    else
  //    {
  //      Ptr = FallbackCore->CompileBlock()
  //      if (Ptr)
  //        Ptr()
  //      else {
  //        ShouldStop = true;
  //      }
  //    }
  // }


  uint64_t VirtualMemorySize = Thread->LookupCache->GetVirtualMemorySize();
  Literal l_VirtualMemory {VirtualMemorySize};
  Literal l_PagePtr {Thread->LookupCache->GetPagePointer()};
  Literal l_CTX {reinterpret_cast<uintptr_t>(CTX)};
  Literal l_Sleep {reinterpret_cast<uint64_t>(SleepThread)};

  uintptr_t CompileBlockPtr{};
  {
    using ClassPtrType = uintptr_t (FEXCore::Context::Context::*)(FEXCore::Core::CpuStateFrame *, uint64_t);
    union PtrCast {
      ClassPtrType ClassPtr;
      uintptr_t Data;
    };

    PtrCast Ptr;
    Ptr.ClassPtr = &FEXCore::Context::Context::CompileBlock;
    CompileBlockPtr = Ptr.Data;
  }

  Literal l_CompileBlock {CompileBlockPtr};
  Literal l_ExitFunctionLink {(uintptr_t)&ExitFunctionLink};

  // Push all the register we need to save
  PushCalleeSavedRegisters();

  // Push our memory base to the correct register
  // Move our thread pointer to the correct register
  // This is passed in to parameter 0 (x0)
  mov(STATE, x0);

  // Save this stack pointer so we can cleanly shutdown the emulation with a long jump
  // regardless of where we were in the stack
  add(x0, sp, 0);
  str(x0, MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, ReturningStackLocation)));

  auto Align16B = [&Buffer, this]() {
    uint64_t CurrentOffset = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());
    for (uint64_t i = (16 - (CurrentOffset & 0xF)); i != 0; i -= 4) {
      nop();
    }
  };

  // used from signals
  AbsoluteLoopTopAddressFillSRA = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());

  FillStaticRegs();

  // We want to ensure that we are 16 byte aligned at the top of this loop
  Align16B();
  aarch64::Label FullLookup{};
  aarch64::Label LoopTop{};
  aarch64::Label ExitSpillSRA{};

  bind(&LoopTop);
  AbsoluteLoopTopAddress = GetLabelAddress<uint64_t>(&LoopTop);

  // Load in our RIP
  // Don't modify x2 since it contains our RIP once the block doesn't exist
  ldr(x2, MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, State.rip)));
  auto RipReg = x2;

  // L1 Cache
  LoadConstant(x0, Thread->LookupCache->GetL1Pointer());

  and_(x3, RipReg, LookupCache::L1_ENTRIES_MASK);
  add(x0, x0, Operand(x3, Shift::LSL, 4));
  ldp(x1, x0, MemOperand(x0));
  cmp(x0, RipReg);
  b(&FullLookup, Condition::ne);
  br(x1);

  // L1C check failed, do a full lookup
  bind(&FullLookup);

  // This is the block cache lookup routine
  // It matches what is going on it LookupCache.h::FindBlock
  ldr(x0, &l_PagePtr);

  // Mask the address by the virtual address size so we can check for aliases
  if (__builtin_popcountl(VirtualMemorySize) == 1) {
    and_(x3, RipReg, Thread->LookupCache->GetVirtualMemorySize() - 1);
  }
  else {
    ldr(x3, &l_VirtualMemory);
    and_(x3, RipReg, x3);
  }

  aarch64::Label NoBlock;
  {
    // Offset the address and add to our page pointer
    lsr(x1, x3, 12);

    // Load the pointer from the offset
    ldr(x0, MemOperand(x0, x1, Shift::LSL, 3));

    // If page pointer is zero then we have no block
    cbz(x0, &NoBlock);

    // Steal the page offset
    and_(x1, x3, 0x0FFF);

    // Shift the offset by the size of the block cache entry
    add(x0, x0, Operand(x1, Shift::LSL, (int)log2(sizeof(FEXCore::LookupCache::LookupCacheEntry))));

    // Load the guest address first to ensure it maps to the address we are currently at
    // This fixes aliasing problems
    ldr(x1, MemOperand(x0, offsetof(FEXCore::LookupCache::LookupCacheEntry, GuestCode)));
    cmp(x1, RipReg);
    b(&NoBlock, Condition::ne);

    // Now load the actual host block to execute if we can
    ldr(x3, MemOperand(x0, offsetof(FEXCore::LookupCache::LookupCacheEntry, HostCode)));
    cbz(x3, &NoBlock);

    // If we've made it here then we have a real compiled block
    {
      // update L1 cache
      LoadConstant(x0, Thread->LookupCache->GetL1Pointer());

      and_(x1, RipReg, LookupCache::L1_ENTRIES_MASK);
      add(x0, x0, Operand(x1, Shift::LSL, 4));
      stp(x3, x2, MemOperand(x0));
      br(x3);
    }
  }

  {
    bind(&ExitSpillSRA);
    ThreadStopHandlerAddressSpillSRA = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());
    SpillStaticRegs();
    ThreadStopHandlerAddress = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());

    PopCalleeSavedRegisters();

    // Return from the function
    // LR is set to the correct return location now
    ret();
  }

  {
    ExitFunctionLinkerAddress = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());

    SpillStaticRegs();

    LoadConstant(x0, (uintptr_t)this);
    mov(x1, STATE);
    mov(x2, lr);

    ldr(x3, &l_ExitFunctionLink);
    blr(x3);

    FillStaticRegs();
    br(x0);
  }

  // Need to create the block
  {
    bind(&NoBlock);

    ldr(x0, &l_CTX);
    mov(x1, STATE);
    ldr(x3, &l_CompileBlock);

    // X2 contains our guest RIP
    SpillStaticRegs();
    blr(x3); // { CTX, Frame, RIP}
    FillStaticRegs();

    // X0 now contains the block pointer
    blr(x0);

    b(&LoopTop);
  }

  {
    Label RestoreContextStateHelperLabel{};
    bind(&RestoreContextStateHelperLabel);
    ThreadSharedData.SignalReturnInstruction = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());

    // Now to get back to our old location we need to do a fault dance
    // We can't use SIGTRAP here since gdb catches it and never gives it to the application!
    hlt(0);
  }

  {
    ThreadPauseHandlerAddressSpillSRA = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());
    SpillStaticRegs();
    ThreadPauseHandlerAddress = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());
    // We are pausing, this means the frontend should be waiting for this thread to idle
    // We will have faulted and jumped to this location at this point

    // Call our sleep handler
    ldr(x0, &l_CTX);
    mov(x1, STATE);
    ldr(x2, &l_Sleep);
    blr(x2);

    PauseReturnInstruction = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());

    // Fault to start running again
    hlt(0);
  }

  {
    // The expectation here is that a thunked function needs to call back in to the JIT in a reentrant safe way
    // To do this safely we need to do some state tracking and register saving
    //
    // eg:
    // JIT Call->
    //  Thunk->
    //    Thunk callback->
    //
    // The thunk callback needs to execute JIT code and when it returns, it needs to safely return to the thunk rather than JIT space
    // This is handled by pushing a return address trampoline to the stack so when the guest address returns it hits our custom thunk return
    //  - This will safely return us to the thunk
    //
    // On return to the thunk, the thunk can get whatever its return value is from the thread context depending on ABI handling on its end
    // When the thunk itself returns, it'll do its regular return logic there
    // void ReentrantCallback(FEXCore::Core::InternalThreadState *Thread, uint64_t RIP);
    CallbackPtr = Buffer->GetOffsetAddress<CPUBackend::JITCallback>(GetCursorOffset());

    // We expect the thunk to have previously pushed the registers it was using
    PushCalleeSavedRegisters();

    // First thing we need to move the thread state pointer back in to our register
    mov(STATE, x0);

    // Make sure to adjust the refcounter so we don't clear the cache now
    LoadConstant(x0, reinterpret_cast<uint64_t>(&SignalHandlerRefCounter));
    ldr(w2, MemOperand(x0));
    add(w2, w2, 1);
    str(w2, MemOperand(x0));

    // Now push the callback return trampoline to the guest stack
    // Guest will be misaligned because calling a thunk won't correct the guest's stack once we call the callback from the host
    LoadConstant(x0, CTX->X86CodeGen.CallbackReturn);

    ldr(x2, MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, State.gregs[X86State::REG_RSP])));
    sub(x2, x2, 16);
    str(x2, MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, State.gregs[X86State::REG_RSP])));

    // Store the trampoline to the guest stack
    // Guest stack is now correctly misaligned after a regular call instruction
    str(x0, MemOperand(x2));

    // Store RIP to the context state
    str(x1, MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, State.rip)));

    // load static regs
    FillStaticRegs();

    // Now go back to the regular dispatcher loop
    b(&LoopTop);
  }

  place(&l_VirtualMemory);
  place(&l_PagePtr);
  place(&l_CTX);
  place(&l_Sleep);
  place(&l_CompileBlock);
  place(&l_ExitFunctionLink);

  FinalizeCode();
  uint64_t CodeEnd = Buffer->GetOffsetAddress<uint64_t>(GetCursorOffset());
  CPU.EnsureIAndDCacheCoherency(reinterpret_cast<void*>(DispatchPtr), CodeEnd - reinterpret_cast<uint64_t>(DispatchPtr));

#if ENABLE_JITSYMBOLS
  std::string Name = "Dispatch_" + std::to_string(::gettid());
  CTX->Symbols.Register(reinterpret_cast<void*>(DispatchPtr), CodeEnd - reinterpret_cast<uint64_t>(DispatchPtr), Name);
#endif

  *GetBuffer() = OriginalBuffer;
}

void Arm64JITCore::SpillStaticRegs() {
  for (size_t i = 0; i < SRA64.size(); i+=2) {
      stp(SRA64[i], SRA64[i+1], MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, State.gregs[i])));
  }

  for (size_t i = 0; i < SRAFPR.size(); i+=2) {
    stp(SRAFPR[i].Q(), SRAFPR[i+1].Q(), MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, State.xmm[i][0])));
  }
}

void Arm64JITCore::FillStaticRegs() {
  for (size_t i = 0; i < SRA64.size(); i+=2) {
    ldp(SRA64[i], SRA64[i+1], MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, State.gregs[i])));
  }

  for (size_t i = 0; i < SRAFPR.size(); i+=2) {
    ldp(SRAFPR[i].Q(), SRAFPR[i+1].Q(), MemOperand(STATE, offsetof(FEXCore::Core::CpuStateFrame, State.xmm[i][0])));
  }
}

void Arm64JITCore::PushDynamicRegsAndLR() {
  uint64_t SPOffset = AlignUp((RA64.size() + 1) * 8 + RAFPR.size() * 16, 16);

  sub(sp, sp, SPOffset);
  int i = 0;

  for (auto RA : RAFPR)
  {
    str(RA.Q(), MemOperand(sp, i * 8));
    i+=2;
  }

#if 0 // All GPRs should be caller saved
  for (auto RA : RA64)
  {
    str(RA, MemOperand(sp, i * 8));
    i++;
  }
#endif

  str(lr, MemOperand(sp, i * 8));
}

void Arm64JITCore::PopDynamicRegsAndLR() {
  uint64_t SPOffset = AlignUp((RA64.size() + 1) * 8 + RAFPR.size() * 16, 16);
  int i = 0;

  for (auto RA : RAFPR)
  {
    ldr(RA.Q(), MemOperand(sp, i * 8));
    i+=2;
  }

#if 0 // All GPRs should be caller saved
  for (auto RA : RA64)
  {
    ldr(RA, MemOperand(sp, i * 8));
    i++;
  }
#endif

  ldr(lr, MemOperand(sp, i * 8));

  add(sp, sp, SPOffset);
}

void Arm64JITCore::ResetStack() {
  if (SpillSlots == 0)
    return;

  if (IsImmAddSub(SpillSlots * 16)) {
    add(sp, sp, SpillSlots * 16);
  } else {
   // Too big to fit in a 12bit immediate
   LoadConstant(x0, SpillSlots * 16);
   add(sp, sp, x0);
  }
}

FEXCore::CPU::CPUBackend *CreateArm64JITCore(FEXCore::Context::Context *ctx, FEXCore::Core::InternalThreadState *Thread, bool CompileThread) {
  return new Arm64JITCore(ctx, Thread, Arm64JITCore::AllocateNewCodeBuffer(Arm64JITCore::INITIAL_CODE_SIZE), CompileThread);
}
}
