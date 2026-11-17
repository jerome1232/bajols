using Bajols.Protocol;

namespace Bajols.Simulator;

/// <summary>Deterministic BCP peer and safety model, not firmware or a physical ballast simulation.</summary>
public sealed class SimulatedVehicle
{
    public static readonly OpaqueId128 DefaultVehicleId = OpaqueId128.FromBytes(Convert.FromHexString("42414A4F4C532D53494D554C41544F52"));
    private readonly Dictionary<uint, (BcpMessage Message, CommandResult Result)> results = [];
    private readonly Queue<uint> resultOrder = [];
    private readonly Dictionary<uint, uint> tickets = [];
    private readonly Queue<uint> ticketOrder = [];
    private uint boot = 1, sessionCounter, outbound, ticketId, highestRequest;
    private uint lastTicket, lastStatus, lastTelemetry, lastHeartbeat, pendingSince, armedSince;
    private uint movementAt, movementTicketAt, buoyancyAt, buoyancyTicketAt, movementSequence, buoyancySequence;
    private uint? neutralSince, releaseSince;
    private bool acceptedInGeneration;
    private VehicleStatus? previousStatusShape;
    private bool hasMovement, hasBuoyancy, established, pending, emergencyLatch, depthHealthy = true, restricted;
    private SessionId session, pendingSession;
    private OpaqueId128 nonce, pendingNonce;
    private uint pendingOutbound;
    private short forward, yaw, dive;
    private BuoyancyTarget target;
    private uint? lastMotionAt;
    private double depthMetres = 2;
    public double HeadingDegrees { get; private set; } = 90;
    public double PitchDegrees { get; private set; }
    private uint revision = 1, lastSentRevision, depthFaultOccurrences, lastFault;
    private bool reportedDepthFault;
    private ArmingBlockers previousBlockers;
    private ushort neutralProgress;
    public OpaqueId128 VehicleId { get; } = DefaultVehicleId;
    public SafetyState State { get; private set; } = SafetyState.Disarmed;
    public MovementOwner Owner { get; private set; } = MovementOwner.Pc;
    public uint Generation { get; private set; } = 1;
    public SafetyCause Cause { get; private set; } = SafetyCause.Startup;
    public int DepthMm { get; private set; } = 2000;
    public bool HasSession => established;
    public SessionId Session => session;
    public bool SuppressTickets { get; set; }
    public bool SuppressStatus { get; set; }
    public bool SuppressTelemetry { get; set; }
    public bool DropIncomingMovement { get; set; }
    public bool DepthHealthy => depthHealthy;
    public short ThrustEffort => State == SafetyState.Armed && hasMovement ? forward : (short)0;
    public short YawEffort => State == SafetyState.Armed && hasMovement ? yaw : (short)0;
    public short DiveEffort => State == SafetyState.Armed && hasMovement && (!restricted || dive <= 0) ? dive : (short)0;
    public BuoyancyAction Buoyancy => State == SafetyState.EmergencyStopLatched ? BuoyancyAction.EmergencyEvacuation :
        State is SafetyState.CommandLossFailsafe or SafetyState.FaultFailsafe || State == SafetyState.Armed && (!hasBuoyancy || restricted && target == BuoyancyTarget.Negative)
        ? BuoyancyAction.SeekNeutral : State != SafetyState.Armed ? BuoyancyAction.None :
        target switch { BuoyancyTarget.Positive => BuoyancyAction.SeekPositive, BuoyancyTarget.Negative => BuoyancyAction.SeekNegative, _ => BuoyancyAction.SeekNeutral };
    private static uint Age(uint now, uint then) => unchecked(now - then);
    private bool MovementFresh(uint now) => hasMovement && Age(now, movementAt) < 500 && Age(now, movementTicketAt) < 500;
    private bool BuoyancyFresh(uint now) => hasBuoyancy && Age(now, buoyancyAt) < 500 && Age(now, buoyancyTicketAt) < 500;

    public void SetOwner(MovementOwner owner, uint now)
    {
        if (!Enum.IsDefined(owner)) throw new ArgumentOutOfRangeException(nameof(owner));
        if (Owner == owner) return;
        Owner = owner;
        if (State == SafetyState.Armed) { State = SafetyState.Disarmed; Cause = SafetyCause.OwnerSelectorChanged; }
        ChangeGeneration(); revision++; Safety(now);
    }
    public void SetDepth(int millimetres, bool healthy, uint now)
    {
        if (millimetres < 0) throw new ArgumentOutOfRangeException(nameof(millimetres));
        DepthMm = millimetres; depthMetres = millimetres / 1000d;
        if (!healthy && depthHealthy && depthFaultOccurrences < uint.MaxValue) depthFaultOccurrences++;
        depthHealthy = healthy;
        if (!healthy && State is SafetyState.Armed or SafetyState.CommandLossFailsafe)
            Enter(SafetyState.FaultFailsafe, SafetyCause.DepthUnavailable);
        // Simulation-only profile: intervene at 4.5 m, release below 4 m. Not hardware calibration.
        if (millimetres >= 4500 && !restricted)
        {
            restricted = true; ChangeGeneration(preserveMovement: true); hasBuoyancy = false; revision++;
        }
        Safety(now);
    }
    public bool LocalEmergencyReset(uint now)
    {
        if (State != SafetyState.EmergencyStopLatched) return false;
        // The simulated procedure completes immediately; real hardware needs independent completion evidence.
        emergencyLatch = false; State = SafetyState.Disarmed; Cause = SafetyCause.LocalEmergencyStopReset;
        ChangeGeneration(); Safety(now); return true;
    }
    public void Reboot(uint now)
    {
        if (boot == uint.MaxValue) throw CounterFault("Boot identity exhausted; service required.");
        boot++; sessionCounter = 0; established = pending = false; results.Clear(); resultOrder.Clear(); highestRequest = 0;
        State = emergencyLatch ? SafetyState.EmergencyStopLatched : SafetyState.Disarmed;
        Cause = emergencyLatch ? SafetyCause.EmergencyStopAsserted : SafetyCause.Startup;
        ChangeGeneration(); Safety(now);
    }
    private void Enter(SafetyState state, SafetyCause cause)
    {
        if (State == SafetyState.EmergencyStopLatched || State == state) return;
        if (State == SafetyState.FaultFailsafe && state == SafetyState.CommandLossFailsafe) return;
        State = state; Cause = cause; emergencyLatch |= state == SafetyState.EmergencyStopLatched;
        ChangeGeneration(); revision++;
    }
    private void ChangeGeneration(bool preserveMovement = false)
    {
        if (Generation == uint.MaxValue) throw CounterFault("Control counter exhausted.");
        Generation++; acceptedInGeneration = false; tickets.Clear(); ticketOrder.Clear(); neutralSince = releaseSince = null;
        hasBuoyancy = false;
        if (!preserveMovement) { hasMovement = false; forward = yaw = dive = 0; }
        revision++;
    }
    private void Safety(uint now)
    {
        if (pending && Age(now, pendingSince) >= 2000) pending = false;
        bool fresh = MovementFresh(now);
        if (State == SafetyState.Armed && (!depthHealthy)) Enter(SafetyState.FaultFailsafe, SafetyCause.DepthUnavailable);
        if (State == SafetyState.Armed && ((!fresh && hasMovement) || (!hasMovement && Age(now, armedSince) >= 500)))
            Enter(SafetyState.CommandLossFailsafe, SafetyCause.MovementExpired);
        if (!fresh) { neutralSince = null; hasMovement = false; }
        if (!BuoyancyFresh(now)) hasBuoyancy = false;
        neutralProgress = neutralSince is uint start && fresh ? (ushort)Math.Min(1000u, Age(now, start)) : (ushort)0;
        if (restricted && State == SafetyState.Armed && depthHealthy && DepthMm < 4000 && fresh &&
            hasBuoyancy && target == BuoyancyTarget.Neutral && dive <= 0)
        {
            releaseSince ??= now;
            if (Age(now, releaseSince.Value) >= 1000) { restricted = false; ChangeGeneration(preserveMovement: true); revision++; }
        }
        else releaseSince = null;
    }
    private bool TryTicket(uint id, uint generation, uint now, out uint issued, out CommandResultCode failure)
    {
        issued = 0;
        if (generation != Generation) { failure = CommandResultCode.WrongControlGeneration; return false; }
        if (!tickets.TryGetValue(id, out issued) || Age(now, issued) >= 500)
        { failure = CommandResultCode.UnknownOrExpiredTicket; return false; }
        failure = CommandResultCode.Accepted; return true;
    }
    public IReadOnlyList<BcpPacket> Receive(BcpPacket packet, uint now)
    {
        Safety(now);
        Span<byte> validation = stackalloc byte[116];
        if (!BcpCodec.TryEncode(packet, WireDirection.PcToVehicle, validation, out _, out _)) return [];
        var output = new List<BcpPacket>(5);
        if (packet.Message is Hello hello)
        {
            if (packet.Session != default || packet.Sequence != 0 || hello.RequestedVersion != 1) return output;
            if (established && hello.Nonce == nonce) { output.Add(Emit(new HelloReply(nonce, 1, VehicleId))); return output; }
            if (pending)
            {
                if (hello.Nonce == pendingNonce) output.Add(PendingReply());
                return output;
            }
            if (State == SafetyState.Armed && Owner == MovementOwner.Pc) return output;
            if (sessionCounter == uint.MaxValue) throw CounterFault("Session counter exhausted.");
            pending = true; pendingSince = now; pendingNonce = hello.Nonce; pendingSession = new(boot, ++sessionCounter); pendingOutbound = 0;
            output.Add(PendingReply()); return output;
        }
        if (packet.Message is SessionConfirm confirm)
        {
            if (established && packet.Session == session && confirm.Nonce == nonce)
            { output.Add(Emit(new SessionReady(Generation))); return output; }
            if (!pending || packet.Session != pendingSession || confirm.Nonce != pendingNonce || packet.Sequence == 0 ||
                State == SafetyState.Armed && Owner == MovementOwner.Pc) return output;
            session = pendingSession; nonce = pendingNonce; outbound = pendingOutbound; established = true; pending = false;
            highestRequest = movementSequence = buoyancySequence = ticketId = 0; results.Clear(); resultOrder.Clear(); tickets.Clear(); ticketOrder.Clear();
            if (Owner == MovementOwner.Pc) { hasMovement = hasBuoyancy = false; neutralSince = null; }
            output.Add(Emit(new SessionReady(Generation))); output.Add(Emit(GetCapabilities())); output.Add(Emit(Status(now))); return output;
        }
        if (!established || packet.Session != session || packet.Sequence == 0) return output;
        uint before = revision;
        switch (packet.Message)
        {
            case MovementCommand m when !DropIncomingMovement:
                if (Owner != MovementOwner.Pc || packet.Sequence <= movementSequence || State == SafetyState.EmergencyStopLatched ||
                    restricted && m.Dive > 0 || !TryTicket(m.TicketId, m.ControlGeneration, now, out uint issued, out _)) break;
                movementSequence = packet.Sequence; hasMovement = acceptedInGeneration = true; movementAt = now; movementTicketAt = issued;
                forward = m.Forward; yaw = m.Yaw; dive = m.Dive;
                if (Math.Abs((int)forward) <= 50 && Math.Abs((int)yaw) <= 50 && Math.Abs((int)dive) <= 50) neutralSince ??= now;
                else neutralSince = null;
                break;
            case BuoyancyCommand b:
                if (State != SafetyState.Armed || Owner != MovementOwner.Pc || packet.Sequence <= buoyancySequence ||
                    restricted && b.Target == BuoyancyTarget.Negative ||
                    !TryTicket(b.TicketId, b.ControlGeneration, now, out uint bIssued, out _)) break;
                buoyancySequence = packet.Sequence; hasBuoyancy = true; buoyancyAt = now; buoyancyTicketAt = bIssued; target = b.Target; break;
            case Arm or Disarm or EmergencyStop or ClearFailsafe:
                output.Add(Emit(Discrete(packet.Message, now))); break;
            case Bajols.Protocol.GetCapabilities: output.Add(Emit(GetCapabilities())); break;
        }
        Safety(now);
        if (before != revision && !SuppressStatus) output.Add(Emit(Status(now)));
        return output;
    }
    private BcpPacket PendingReply()
    {
        if (pendingOutbound == uint.MaxValue) throw CounterFault("Pending counter exhausted.");
        return new(pendingSession, ++pendingOutbound, new HelloReply(pendingNonce, 1, VehicleId));
    }
    private CommandResult Discrete(BcpMessage message, uint now)
    {
        uint id = message switch { Arm m => m.RequestId, Disarm m => m.RequestId, EmergencyStop m => m.RequestId, ClearFailsafe m => m.RequestId, _ => 0 };
        if (results.TryGetValue(id, out var previous)) return previous.Message == message ? previous.Result : new(id, message.Type, CommandResultCode.RequestIdReused, Generation);
        if (id == 0 || id <= highestRequest) return new(id, message.Type, CommandResultCode.RequestTooOld, Generation);
        highestRequest = id;
        var code = CommandResultCode.Accepted;
        if (message is EmergencyStop) Enter(SafetyState.EmergencyStopLatched, SafetyCause.EmergencyStopAsserted);
        else if (message is Disarm)
        {
            if (State == SafetyState.Armed) { State = SafetyState.Disarmed; Cause = SafetyCause.ExplicitDisarm; ChangeGeneration(); }
        }
        else if (State == SafetyState.EmergencyStopLatched) code = CommandResultCode.LocalResetRequired;
        else if (Owner != MovementOwner.Pc) code = CommandResultCode.NotMovementOwner;
        else
        {
            uint ticket = message is Arm a ? a.TicketId : ((ClearFailsafe)message).TicketId;
            uint generation = message is Arm ar ? ar.ControlGeneration : ((ClearFailsafe)message).ControlGeneration;
            if (!TryTicket(ticket, generation, now, out _, out code)) { }
            else if (!depthHealthy) code = CommandResultCode.BlockingFault;
            else if (!MovementFresh(now)) code = CommandResultCode.NeutralHoldIncomplete;
            else if (message is Arm)
            {
                if (State != SafetyState.Disarmed) code = CommandResultCode.InvalidSafetyState;
                else if (!MovementFresh(now) || neutralProgress < 1000) code = CommandResultCode.NeutralHoldIncomplete;
                else { State = SafetyState.Armed; Cause = SafetyCause.ExplicitArm; armedSince = now; ChangeGeneration(); }
            }
            else if (State is not (SafetyState.CommandLossFailsafe or SafetyState.FaultFailsafe)) code = CommandResultCode.InvalidSafetyState;
            else { Cause = State == SafetyState.FaultFailsafe ? SafetyCause.FaultFailsafeCleared : SafetyCause.CommandLossFailsafeCleared; State = SafetyState.Disarmed; ChangeGeneration(); }
        }
        var result = new CommandResult(id, message.Type, code, Generation);
        if (results.Count == 16) results.Remove(resultOrder.Dequeue());
        results[id] = (message, result); resultOrder.Enqueue(id); return result;
    }
    public IReadOnlyList<BcpPacket> Tick(uint now)
    {
        Safety(now); AdvanceMotion(now); var output = new List<BcpPacket>(5);
        if (!established) return output;
        if (Age(now, lastTicket) >= 50 && !SuppressTickets)
        {
            lastTicket = now;
            if (ticketId == uint.MaxValue) { Enter(SafetyState.FaultFailsafe, SafetyCause.OtherBlockingFault); established = false; return output; }
            uint id = ++ticketId;
            if (tickets.Count == 16) tickets.Remove(ticketOrder.Dequeue());
            tickets[id] = now; ticketOrder.Enqueue(id); output.Add(Emit(new ControlTicket(id, Generation, 500)));
        }
        var currentStatus = Status(now);
        if ((Age(now, lastStatus) >= 200 || currentStatus.StatusRevision != lastSentRevision) && !SuppressStatus)
        { lastStatus = now; output.Add(Emit(currentStatus)); }
        bool depthFault = !depthHealthy;
        if (depthFault != reportedDepthFault || depthFault && Age(now, lastFault) >= 1000)
        {
            lastFault = now; reportedDepthFault = depthFault;
            output.Add(Emit(new Fault(FaultCode.DepthUnavailable, FaultSeverity.Blocking, depthFault, depthFaultOccurrences)));
        }
        if (Age(now, lastTelemetry) >= 200 && !SuppressTelemetry)
        {
            lastTelemetry = now;
            var quality = TelemetryQuality.Valid | TelemetryQuality.Simulated;
            output.Add(Emit(new Telemetry(new TelemetryRecord[] {
                new(MeasurementId.Depth, depthHealthy ? quality : TelemetryQuality.SensorFault | TelemetryQuality.Simulated, 0, DepthMm),
                new(MeasurementId.SupplyVoltage, quality, 0, 12000),
                new(MeasurementId.Heading, quality, 0, (int)Math.Round(HeadingDegrees * 100) % 36000),
                new(MeasurementId.Pitch, quality, 0, (int)Math.Round(PitchDegrees * 100)),
                new(MeasurementId.PropulsionRpm, quality, 0, Math.Abs(ThrustEffort) * 3) })));
        }
        if (Age(now, lastHeartbeat) >= 1000) { lastHeartbeat = now; output.Add(Emit(new Heartbeat())); }
        return output;
    }
    // Illustrative kinematics only: not hydrodynamics or a validated ballast controller.
    private void AdvanceMotion(uint now)
    {
        double dt = lastMotionAt is uint previous ? Math.Min(0.1, Age(now, previous) / 1000d) : 0;
        lastMotionAt = now;
        double targetPitch = -DiveEffort / 1000d * 25;
        PitchDegrees += (targetPitch - PitchDegrees) * (1 - Math.Exp(-dt * 4));
        double steeringAuthority = Math.Abs(ThrustEffort) / 1000d;
        HeadingDegrees = (HeadingDegrees + YawEffort / 1000d * steeringAuthority * 30 * dt + 360) % 360;
        double ballastVelocity = Buoyancy switch {
            BuoyancyAction.SeekPositive => -0.15, BuoyancyAction.SeekNegative => 0.15,
            BuoyancyAction.EmergencyEvacuation => -0.7, _ => 0 };
        double forwardVelocity = ThrustEffort / 1000d * 0.6;
        depthMetres = Math.Max(0, depthMetres + (-forwardVelocity * Math.Sin(PitchDegrees * Math.PI / 180) + ballastVelocity) * dt);
        int depth = (int)Math.Round(depthMetres * 1000);
        if (depth != DepthMm)
        {
            double precise = depthMetres;
            SetDepth(depth, depthHealthy, now); // The existing depth interlock remains authoritative.
            depthMetres = precise;
        }
    }
    private InvalidOperationException CounterFault(string message)
    {
        if (State != SafetyState.EmergencyStopLatched) State = SafetyState.FaultFailsafe;
        hasMovement = hasBuoyancy = established = false;
        return new InvalidOperationException(message);
    }
    private BcpPacket Emit(BcpMessage message)
    {
        if (message is VehicleStatus sent) lastSentRevision = sent.StatusRevision;
        if (outbound == uint.MaxValue) throw CounterFault("Packet counter exhausted.");
        return new(session, ++outbound, message);
    }
    public static Capabilities GetCapabilities() => new(1, 1, 7, 15, 0x3d, 50, 500, 50, 500, 1000, 50);
    public VehicleStatus Status(uint now)
    {
        Safety(now);
        ArmingBlockers blockers = ArmingBlockers.None;
        if (Owner == MovementOwner.None) blockers |= ArmingBlockers.NoOwner;
        if (!MovementFresh(now)) blockers |= ArmingBlockers.StaleOwner;
        if (hasMovement && (Math.Abs((int)forward) > 50 || Math.Abs((int)yaw) > 50 || Math.Abs((int)dive) > 50)) blockers |= ArmingBlockers.NonNeutralInput;
        if (neutralProgress < 1000) blockers |= ArmingBlockers.NeutralHoldIncomplete;
        if (!depthHealthy) blockers |= ArmingBlockers.BlockingFault | ArmingBlockers.RequiredSubsystemUnavailable;
        blockers |= State switch { SafetyState.CommandLossFailsafe => ArmingBlockers.CommandLossLatch, SafetyState.FaultFailsafe => ArmingBlockers.FaultFailsafeLatch, SafetyState.EmergencyStopLatched => ArmingBlockers.EmergencyLatch, _ => ArmingBlockers.None };
        if (blockers != previousBlockers) { revision++; previousBlockers = blockers; }
        ushort remaining = MovementFresh(now) ? (ushort)Math.Min(500 - Age(now, movementAt), 500 - Age(now, movementTicketAt)) : (ushort)0;
        ushort bRemaining = BuoyancyFresh(now) ? (ushort)Math.Min(500 - Age(now, buoyancyAt), 500 - Age(now, buoyancyTicketAt)) : (ushort)0;
        ActiveRestrictions restrictions = restricted ? ActiveRestrictions.DepthLimitActive | ActiveRestrictions.DownwardControlInhibited : ActiveRestrictions.None;
        if (!depthHealthy) restrictions |= ActiveRestrictions.DepthUnavailable | ActiveRestrictions.DownwardControlInhibited;
        var snapshot = new VehicleStatus(revision, now, State, Owner, ControlMode.Effort, Generation, blockers, neutralProgress,
            acceptedInGeneration ? (ushort)Math.Min(65534u, Age(now, movementAt)) : ushort.MaxValue, remaining,
            Buoyancy, !depthHealthy && Buoyancy == BuoyancyAction.SeekNeutral ? ActionStatus.Unavailable : Buoyancy == BuoyancyAction.None ? ActionStatus.Inactive : State == SafetyState.EmergencyStopLatched ? ActionStatus.Completed : ActionStatus.Running,
            Cause, EstimatedBuoyancy.Unknown, !hasBuoyancy ? AuthorizedBuoyancyTarget.None : target switch { BuoyancyTarget.Positive => AuthorizedBuoyancyTarget.Positive, BuoyancyTarget.Negative => AuthorizedBuoyancyTarget.Negative, _ => AuthorizedBuoyancyTarget.Neutral }, bRemaining, restrictions);
        var shape = snapshot with { StatusRevision = 0, VehicleUptimeMs = 0, NeutralHoldProgressMs = 0,
            MovementAgeMs = 0, MovementTimeRemainingMs = 0, BuoyancyCommandTimeRemainingMs = 0 };
        if (shape != previousStatusShape) { revision++; previousStatusShape = shape; }
        return snapshot with { StatusRevision = revision };
    }
}
