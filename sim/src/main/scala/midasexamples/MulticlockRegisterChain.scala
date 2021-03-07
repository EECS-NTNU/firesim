//See LICENSE for license details.

package firesim.midasexamples

import chisel3._
import freechips.rocketchip.config.{Field, Parameters, Config}

import midas.widgets.{RationalClockBridge, RationalClock, PeekPokeBridge, BridgeableClockMux, BridgeableClockDivider}

/**
  * Creates a scan chain in which each successive stage is placed in a different
  * clock domain.  This serves as a simple test of the scalability of FireSim's multiclock 
  * support.
  *
  * @param A parameters instance with the Field [[NumClockDomains]] set.
  */
class MulticlockRegisterChain(implicit p: Parameters) extends RawModule {

  class RegisterModule extends MultiIOModule {
    def dataType = UInt(32.W)
    val in = IO(Input(dataType))
    val out = IO(Output(dataType))
    out := RegNext(in)
  }

  val reset = WireInit(false.B)
  val fullRate = RationalClockBridge().io.clocks.head

  val regMods = Seq.tabulate(p(NumClockDomains)){ i =>
    // Placeholder target module
    val clockDivider = Module(new freechips.rocketchip.util.ClockDivider2)
    clockDivider.io.clk_in := fullRate
    val localClock = clockDivider.io.clk_out

    // Annotate the divider indicating it can be replaced with a model
    BridgeableClockDivider(
      clockDivider,
      clockDivider.io.clk_in,
      clockDivider.io.clk_out,
      div = i + 1)

    withClockAndReset(localClock, reset) {
      val regMod = Module(new RegisterModule())
      regMod
    }
  }

  def connect(mods: List[RegisterModule]): Unit = mods match {
    case Nil => Nil
    case modA :: Nil => Nil
    case modA :: mods =>
      mods.head.in := modA.out
      connect(mods)
  }
  connect(regMods.toList)

  withClock(fullRate) {
    val peekPokeBridge = PeekPokeBridge(fullRate,
                                        reset,
                                        ("in", regMods.head.in),
                                        ("out", regMods.last.out))
  }
}
