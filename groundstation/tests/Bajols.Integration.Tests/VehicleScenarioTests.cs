using Bajols.Protocol;
using Bajols.Simulator;
using Xunit;

namespace Bajols.Integration.Tests;

public sealed class VehicleScenarioTests
{
    private sealed class Peer
    {
        public SimulatedVehicle Vehicle { get; } = new();
        public SessionId Session;
        public ControlTicket Ticket = null!;
        public uint Now, Sequence = 1, Request;
        public Peer()
        {
            var nonce = new OpaqueId128(1, 2);
            Session = Assert.Single(Vehicle.Receive(new(default, 0, new Hello(nonce, 1)), 0)).Session;
            Vehicle.Receive(new(Session, 1, new SessionConfirm(nonce)), 0);
        }
        public IReadOnlyList<BcpPacket> At(uint now)
        {
            Now = now; var output = Vehicle.Tick(now);
            foreach (var p in output) if (p.Message is ControlTicket ticket) Ticket = ticket;
            return output;
        }
        public IReadOnlyList<BcpPacket> Send(BcpMessage message) => Vehicle.Receive(new(Session, ++Sequence, message), Now);
        public void Move(short f = 0, short y = 0, short d = 0) => Send(new MovementCommand(Ticket.TicketId, Ticket.ControlGeneration, f, y, d));
        public CommandResult Action(MessageType type)
        {
            uint id = ++Request;
            BcpMessage m = type switch { MessageType.Arm => new Arm(id, Ticket.TicketId, Ticket.ControlGeneration),
                MessageType.ClearFailsafe => new ClearFailsafe(id, Ticket.TicketId, Ticket.ControlGeneration),
                MessageType.Disarm => new Disarm(id), _ => new EmergencyStop(id) };
            return (CommandResult)Send(m).First(p => p.Message is CommandResult).Message;
        }
        public void Arm()
        {
            for (uint t = 50; t <= 1050; t += 50) { At(t); Move(); }
            Assert.Equal(CommandResultCode.Accepted, Action(MessageType.Arm).Result);
            At(1100); Move();
        }
    }
    [Fact]
    public void Neutral_hold_is_continuous_and_arm_does_not_apply_previous_intent()
    {
        var p = new Peer();
        for (uint t = 50; t <= 1000; t += 50) { p.At(t); p.Move(50, -50); }
        Assert.Equal(CommandResultCode.NeutralHoldIncomplete, p.Action(MessageType.Arm).Result);
        p.At(1050); p.Move(50, -50); uint old = p.Vehicle.Generation;
        Assert.Equal(CommandResultCode.Accepted, p.Action(MessageType.Arm).Result);
        Assert.Equal(old + 1, p.Vehicle.Generation); Assert.Equal(0, p.Vehicle.ThrustEffort);
        p.At(1550); Assert.Equal(SafetyState.CommandLossFailsafe, p.Vehicle.State);
    }
    [Fact]
    public void Non_neutral_input_resets_the_hold()
    {
        var p = new Peer();
        for (uint t = 50; t <= 1050; t += 50) { p.At(t); p.Move(t == 1000 ? (short)51 : (short)0); }
        Assert.Equal(CommandResultCode.NeutralHoldIncomplete, p.Action(MessageType.Arm).Result);
    }
    [Fact]
    public void Heartbeats_do_not_extend_ticket_bounded_movement()
    {
        var p = new Peer(); p.Arm(); p.Move(800);
        for (uint t = 1150; t <= 1550; t += 50) { p.At(t); p.Send(new Heartbeat()); }
        Assert.Equal(SafetyState.Armed, p.Vehicle.State);
        p.At(1600); Assert.Equal(SafetyState.CommandLossFailsafe, p.Vehicle.State);
        Assert.Equal(0, p.Vehicle.ThrustEffort); Assert.Equal(BuoyancyAction.SeekNeutral, p.Vehicle.Buoyancy);
    }
    [Fact]
    public void Old_ticket_command_does_not_get_an_extra_500_ms()
    {
        var p = new Peer(); p.Arm(); var old = p.Ticket;
        for (uint t = 1150; t <= 1450; t += 50) { p.At(t); p.Move(); }
        p.At(1500); p.Send(new MovementCommand(old.TicketId, old.ControlGeneration, 700, 0, 0));
        Assert.Equal(700, p.Vehicle.ThrustEffort);
        p.At(1599); Assert.Equal(SafetyState.Armed, p.Vehicle.State);
        p.At(1600); Assert.Equal(SafetyState.CommandLossFailsafe, p.Vehicle.State);
    }
    [Fact]
    public void Replay_and_wrong_session_do_not_refresh_movement()
    {
        var p = new Peer(); p.Arm();
        var replay = new BcpPacket(p.Session, p.Sequence, new MovementCommand(p.Ticket.TicketId, p.Ticket.ControlGeneration, 900, 0, 0));
        p.At(1450); p.Vehicle.Receive(replay, p.Now);
        p.Vehicle.Receive(replay with { Session = new(99, 99), Sequence = 10000 }, p.Now);
        p.At(1600); Assert.Equal(SafetyState.CommandLossFailsafe, p.Vehicle.State);
    }
    [Fact]
    public void Buoyancy_and_movement_have_independent_expiry()
    {
        var p = new Peer(); p.Arm(); p.Send(new BuoyancyCommand(p.Ticket.TicketId, p.Ticket.ControlGeneration, BuoyancyTarget.Negative));
        Assert.Equal(BuoyancyAction.SeekNegative, p.Vehicle.Buoyancy);
        for (uint t = 1150; t <= 1600; t += 50) { p.At(t); p.Move(); }
        Assert.Equal(SafetyState.Armed, p.Vehicle.State); Assert.Equal(BuoyancyAction.SeekNeutral, p.Vehicle.Buoyancy);
        for (uint t = 1650; t <= 2100; t += 50) { p.At(t); p.Send(new BuoyancyCommand(p.Ticket.TicketId, p.Ticket.ControlGeneration, BuoyancyTarget.Positive)); }
        Assert.Equal(SafetyState.CommandLossFailsafe, p.Vehicle.State);
    }
    [Fact]
    public void Emergency_is_latched_across_reboot_and_only_locally_reset()
    {
        var p = new Peer(); p.Arm(); p.Action(MessageType.EmergencyStop);
        Assert.Equal(SafetyState.EmergencyStopLatched, p.Vehicle.State);
        Assert.Equal(BuoyancyAction.EmergencyEvacuation, p.Vehicle.Buoyancy);
        uint generation = p.Vehicle.Generation; p.Action(MessageType.EmergencyStop); p.Action(MessageType.Disarm);
        Assert.Equal(generation, p.Vehicle.Generation);
        Assert.Equal(CommandResultCode.LocalResetRequired, p.Action(MessageType.ClearFailsafe).Result);
        p.Vehicle.Reboot(p.Now); Assert.Equal(SafetyState.EmergencyStopLatched, p.Vehicle.State);
        Assert.True(p.Vehicle.LocalEmergencyReset(p.Now)); Assert.Equal(SafetyState.Disarmed, p.Vehicle.State);
    }
    [Fact]
    public void Cached_arm_result_cannot_rearm_after_disarm()
    {
        var p = new Peer();
        for (uint t = 50; t <= 1050; t += 50) { p.At(t); p.Move(); }
        var arm = new Arm(1, p.Ticket.TicketId, p.Ticket.ControlGeneration);
        p.Send(arm); p.Send(new Disarm(2));
        var result = Assert.IsType<CommandResult>(p.Send(arm).First().Message);
        Assert.Equal(CommandResultCode.Accepted, result.Result); Assert.Equal(SafetyState.Disarmed, p.Vehicle.State);
        Assert.Equal(CommandResultCode.RequestIdReused, Assert.IsType<CommandResult>(p.Send(new EmergencyStop(1)).First().Message).Result);
        Assert.Equal(SafetyState.Disarmed, p.Vehicle.State);
        for (uint id = 3; id < 22; id++) p.Send(new Disarm(id));
        Assert.Equal(CommandResultCode.RequestTooOld, Assert.IsType<CommandResult>(p.Send(arm).First().Message).Result);
    }
    [Fact]
    public void Source_change_disarms_and_non_owner_cannot_arm_but_can_stop()
    {
        var p = new Peer(); p.Arm(); p.Vehicle.SetOwner(MovementOwner.FlySky, p.Now);
        Assert.Equal(SafetyState.Disarmed, p.Vehicle.State);
        p.At(1150); Assert.Equal(CommandResultCode.NotMovementOwner, p.Action(MessageType.Arm).Result);
        p.Action(MessageType.EmergencyStop); Assert.Equal(SafetyState.EmergencyStopLatched, p.Vehicle.State);
    }
    [Fact]
    public void Downward_packet_is_rejected_whole_and_negative_target_is_not_queued()
    {
        var p = new Peer(); p.Arm(); p.Move(200);
        p.Vehicle.SetDepth(5000, true, p.Now); p.At(1150);
        p.Move(900, 500, 1000); Assert.Equal(200, p.Vehicle.ThrustEffort); Assert.Equal(0, p.Vehicle.YawEffort);
        p.Send(new BuoyancyCommand(p.Ticket.TicketId, p.Ticket.ControlGeneration, BuoyancyTarget.Negative));
        Assert.Equal(BuoyancyAction.SeekNeutral, p.Vehicle.Buoyancy);
        p.At(1600); Assert.Equal(SafetyState.CommandLossFailsafe, p.Vehicle.State);
    }
    [Fact]
    public void Restriction_release_requires_fresh_neutral_hold_then_new_generation()
    {
        var p = new Peer(); p.Arm(); p.Vehicle.SetDepth(5000, true, p.Now); p.Vehicle.SetDepth(2000, true, p.Now);
        uint restrictedGeneration = p.Vehicle.Generation;
        for (uint t = 1150; t <= 2150; t += 50)
        {
            p.At(t); p.Move();
            p.Send(new BuoyancyCommand(p.Ticket.TicketId, p.Ticket.ControlGeneration, BuoyancyTarget.Neutral));
        }
        Assert.True(p.Vehicle.Generation > restrictedGeneration);
        p.At(2200); p.Move(0, 0, 500); Assert.Equal(500, p.Vehicle.DiveEffort);
    }
    [Fact]
    public void Depth_fault_requires_explicit_clear_and_requalification()
    {
        var p = new Peer(); p.Arm(); p.Vehicle.SetDepth(2000, false, p.Now);
        Assert.Equal(SafetyState.FaultFailsafe, p.Vehicle.State); Assert.Equal(0, p.Vehicle.ThrustEffort);
        p.At(1150); p.Move(); Assert.Equal(CommandResultCode.BlockingFault, p.Action(MessageType.ClearFailsafe).Result);
        p.Vehicle.SetDepth(2000, true, p.Now); p.Move();
        Assert.Equal(SafetyState.FaultFailsafe, p.Vehicle.State);
        Assert.Equal(CommandResultCode.Accepted, p.Action(MessageType.ClearFailsafe).Result);
        Assert.Equal(SafetyState.Disarmed, p.Vehicle.State);
        p.At(1200); Assert.Equal(CommandResultCode.NeutralHoldIncomplete, p.Action(MessageType.Arm).Result);
    }
    [Fact]
    public void Handshake_retries_and_historical_confirmations_cannot_replace_active_control()
    {
        var v = new SimulatedVehicle(); var nonce = new OpaqueId128(1, 2);
        var first = Assert.Single(v.Receive(new(default, 0, new Hello(nonce, 1)), 0));
        var retry = Assert.Single(v.Receive(new(default, 0, new Hello(nonce, 1)), 1900));
        Assert.Equal(first.Session, retry.Session);
        Assert.Empty(v.Receive(new(first.Session, 1, new SessionConfirm(nonce)), 2000));
        var next = Assert.Single(v.Receive(new(default, 0, new Hello(nonce, 1)), 2001));
        Assert.NotEqual(first.Session, next.Session);
        Assert.Empty(v.Receive(new(first.Session, 1, new SessionConfirm(nonce)), 2001));
        v.Receive(new(next.Session, 1, new SessionConfirm(nonce)), 2002);
        var repeated = v.Receive(new(next.Session, 2, new SessionConfirm(nonce)), 2003);
        Assert.IsType<SessionReady>(Assert.Single(repeated).Message); Assert.Equal(SafetyState.Disarmed, v.State);
    }

    [Fact]
    public void Ticket_expiry_uses_wrap_safe_onboard_time()
    {
        var p = new Peer(); p.At(0xFFFFFF00); p.Move();
        Assert.Equal((ushort)100, p.Vehicle.Status(0x00000090).MovementTimeRemainingMs);
        Assert.Equal((ushort)0, p.Vehicle.Status(0x000000F4).MovementTimeRemainingMs);
    }

    [Fact]
    public void Capabilities_advertise_only_implemented_simulated_measurements()
    {
        var p = new Peer();
        var sample = Assert.IsType<Telemetry>(p.At(200).Single(packet => packet.Message is Telemetry).Message);
        uint mask = 0;
        foreach (var record in sample.Records) mask |= 1u << ((int)record.MeasurementId - 1);
        Assert.Equal(mask, SimulatedVehicle.GetCapabilities().SupportedTelemetry);
    }

    [Fact]
    public void Accepted_motion_changes_pitch_heading_depth_and_disarm_levels_the_model()
    {
        var p = new Peer(); p.Arm();
        for (uint t = 1150; t <= 2150; t += 50) { p.At(t); p.Move(600, 500, 500); }
        Assert.True(p.Vehicle.PitchDegrees < -5);
        Assert.True(p.Vehicle.HeadingDegrees > 95);
        Assert.True(p.Vehicle.DepthMm > 2000);
        double before = Math.Abs(p.Vehicle.PitchDegrees);
        p.Action(MessageType.Disarm);
        for (uint t = 2200; t <= 3200; t += 50) p.At(t);
        Assert.True(Math.Abs(p.Vehicle.PitchDegrees) < before / 2);
        Assert.Equal(0, p.Vehicle.ThrustEffort);
    }

    [Theory]
    [InlineData(0, 1000)]
    [InlineData(1000, 0)]
    public void Yaw_requires_both_thrust_and_steering(short thrust, short steering)
    {
        var p = new Peer(); p.Arm();
        for (uint t = 1150; t <= 2150; t += 50) { p.At(t); p.Move(thrust, steering); }
        Assert.Equal(90d, p.Vehicle.HeadingDegrees);
    }

    [Fact]
    public void Steering_scales_with_thrust_and_stops_turning_when_thrust_is_zero()
    {
        var half = new Peer(); half.Arm();
        var full = new Peer(); full.Arm();
        for (uint t = 1150; t <= 2150; t += 50)
        {
            half.At(t); half.Move(500, 500);
            full.At(t); full.Move(1000, 500);
        }
        Assert.True(half.Vehicle.HeadingDegrees > 90);
        Assert.Equal((half.Vehicle.HeadingDegrees - 90) * 2, full.Vehicle.HeadingDegrees - 90, 6);
        full.Move(0, 1000);
        double heading = full.Vehicle.HeadingDegrees;
        for (uint t = 2200; t <= 2700; t += 50) { full.At(t); full.Move(0, 1000); }
        Assert.Equal(heading, full.Vehicle.HeadingDegrees);
    }

    [Fact]
    public void Evicted_ticket_cannot_establish_input_freshness()
    {
        var p = new Peer(); p.At(50); var old = p.Ticket;
        for (uint t = 100; t <= 850; t += 50) p.At(t);
        p.Send(new MovementCommand(old.TicketId, old.ControlGeneration, 0, 0, 0));
        Assert.Equal(ushort.MaxValue, p.Vehicle.Status(p.Now).MovementAgeMs);
        Assert.Equal((ushort)0, p.Vehicle.Status(p.Now).NeutralHoldProgressMs);
    }

    [Fact]
    public void Fault_events_repeat_without_incrementing_occurrence_until_reactivation()
    {
        var p = new Peer(); p.Vehicle.SetDepth(2000, false, 50);
        var first = Assert.IsType<Fault>(p.At(50).Single(packet => packet.Message is Fault).Message);
        Assert.True(first.Active); Assert.Equal(1u, first.OccurrenceCounter);
        var repeat = Assert.IsType<Fault>(p.At(1050).Single(packet => packet.Message is Fault).Message);
        Assert.Equal(1u, repeat.OccurrenceCounter);
        p.Vehicle.SetDepth(2000, true, 1100);
        Assert.False(Assert.IsType<Fault>(p.At(1100).Single(packet => packet.Message is Fault).Message).Active);
        p.Vehicle.SetDepth(2000, false, 1200);
        Assert.Equal(2u, Assert.IsType<Fault>(p.At(1200).Single(packet => packet.Message is Fault).Message).OccurrenceCounter);
    }
}
