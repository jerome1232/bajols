using Bajols.Protocol;

namespace Bajols.Simulator;

/// <summary>Deterministic BCP peer and safety model, not firmware or a physical ballast simulation.</summary>
public sealed class SimulatedVehicle
{
    /// <summary>
    /// Stable opaque identity used by the bundled simulated-vehicle profile.
    /// </summary>
    public static readonly OpaqueId128 DefaultVehicleId = OpaqueId128.FromBytes(Convert.FromHexString("42414A4F4C532D53494D554C41544F52"));

    /// <summary>
    /// Retained discrete requests and their historical results, bounded to sixteen entries.
    /// </summary>
    private readonly Dictionary<uint, (BcpMessage Message, CommandResult Result)> results = [];

    /// <summary>
    /// Insertion order used to evict the oldest retained discrete request.
    /// </summary>
    private readonly Queue<uint> resultOrder = [];

    /// <summary>
    /// Outstanding freshness-ticket identifiers mapped to onboard issuance times in milliseconds.
    /// </summary>
    private readonly Dictionary<uint, uint> tickets = [];

    /// <summary>
    /// Issuance order used to bound the outstanding-ticket window to sixteen entries.
    /// </summary>
    private readonly Queue<uint> ticketOrder = [];

    /// <summary>
    /// Simulated boot counter used to prevent reuse of session identities after a model reboot.
    /// </summary>
    private uint boot = 1;

    /// <summary>
    /// Most recently reserved session-identity counter within the current simulated boot.
    /// </summary>
    private uint sessionCounter;

    /// <summary>
    /// Last vehicle-to-PC packet sequence in the established session.
    /// </summary>
    private uint outbound;

    /// <summary>
    /// Last freshness-ticket identifier issued within the current session.
    /// </summary>
    private uint ticketId;

    /// <summary>
    /// Highest processed discrete-request identifier, including entries evicted from the result cache.
    /// </summary>
    private uint highestRequest;

    /// <summary>
    /// Onboard time in milliseconds of the most recent ticket publication.
    /// </summary>
    private uint lastTicket;

    /// <summary>
    /// Onboard time in milliseconds of the most recent scheduled status publication.
    /// </summary>
    private uint lastStatus;

    /// <summary>
    /// Onboard time in milliseconds of the most recent telemetry publication.
    /// </summary>
    private uint lastTelemetry;

    /// <summary>
    /// Onboard time in milliseconds of the most recent diagnostic heartbeat.
    /// </summary>
    private uint lastHeartbeat;

    /// <summary>
    /// Onboard creation time of the pending handshake; retries do not extend it.
    /// </summary>
    private uint pendingSince;

    /// <summary>
    /// Onboard arming time used to bound the wait for the first new-generation movement command.
    /// </summary>
    private uint armedSince;

    /// <summary>
    /// Onboard receipt time of the last accepted movement command.
    /// </summary>
    private uint movementAt;

    /// <summary>
    /// Onboard issuance time of the ticket authorizing the accepted movement command.
    /// </summary>
    private uint movementTicketAt;

    /// <summary>
    /// Onboard receipt time of the last accepted normal buoyancy command.
    /// </summary>
    private uint buoyancyAt;

    /// <summary>
    /// Onboard issuance time of the ticket authorizing the normal buoyancy target.
    /// </summary>
    private uint buoyancyTicketAt;

    /// <summary>
    /// Highest accepted movement packet sequence, tracked independently from buoyancy commands.
    /// </summary>
    private uint movementSequence;

    /// <summary>
    /// Highest accepted buoyancy packet sequence, tracked independently from movement commands.
    /// </summary>
    private uint buoyancySequence;

    /// <summary>
    /// Start of the uninterrupted fresh neutral-input interval, or null when qualification is broken.
    /// </summary>
    private uint? neutralSince;

    /// <summary>
    /// Start of the eligible depth-restriction release interval, or null when qualification is broken.
    /// </summary>
    private uint? releaseSince;

    /// <summary>
    /// Whether movement has been accepted in the current control generation for age reporting.
    /// </summary>
    private bool acceptedInGeneration;

    /// <summary>
    /// Last discrete status projection, excluding ticking ages and hold progress, used to detect changes.
    /// </summary>
    private VehicleStatus? previousStatusShape;

    /// <summary>
    /// Whether accepted movement remains eligible for freshness checks and output application.
    /// </summary>
    private bool hasMovement;

    /// <summary>
    /// Whether a renewable normal buoyancy target remains eligible for freshness checks.
    /// </summary>
    private bool hasBuoyancy;

    /// <summary>
    /// Whether a PC session has completed its explicit handshake.
    /// </summary>
    private bool established;

    /// <summary>
    /// Whether a bounded pending handshake is awaiting confirmation.
    /// </summary>
    private bool pending;

    /// <summary>
    /// In-memory emergency-stop state retained across simulated link loss and model reboots.
    /// </summary>
    private bool emergencyLatch;

    /// <summary>
    /// Whether the injected simulated depth measurement is currently healthy.
    /// </summary>
    private bool depthHealthy = true;

    /// <summary>
    /// Whether downward control remains inhibited pending depth-release qualification.
    /// </summary>
    private bool restricted;

    /// <summary>
    /// Identity of the established PC session.
    /// </summary>
    private SessionId session;

    /// <summary>
    /// Reserved identity of the pending handshake; abandoned identities are never recycled.
    /// </summary>
    private SessionId pendingSession;

    /// <summary>
    /// Connection-attempt nonce associated with the established PC session.
    /// </summary>
    private OpaqueId128 nonce;

    /// <summary>
    /// Connection-attempt nonce associated with the pending handshake.
    /// </summary>
    private OpaqueId128 pendingNonce;

    /// <summary>
    /// Last reply sequence assigned within the pending handshake.
    /// </summary>
    private uint pendingOutbound;

    /// <summary>
    /// Last accepted signed forward effort in BCP units from -1000 to 1000.
    /// </summary>
    private short forward;

    /// <summary>
    /// Last accepted signed starboard-positive turning effort in BCP units.
    /// </summary>
    private short yaw;

    /// <summary>
    /// Last accepted signed downward-positive diving effort in BCP units.
    /// </summary>
    private short dive;

    /// <summary>
    /// Last accepted renewable normal buoyancy target.
    /// </summary>
    private BuoyancyTarget target;

    /// <summary>
    /// Onboard time of the previous illustrative motion step, or null before integration begins.
    /// </summary>
    private uint? lastMotionAt;

    /// <summary>
    /// Continuous simulated depth in metres, retained between integer telemetry updates.
    /// </summary>
    private double depthMetres = 2;

    /// <summary>
    /// Current simulated clockwise magnetic heading in degrees from zero to less than 360.
    /// </summary>
    public double HeadingDegrees { get; private set; } = 90;

    /// <summary>
    /// Current simulated pitch in degrees; positive is nose-up.
    /// </summary>
    public double PitchDegrees
    {
        get; private set;
    }

    /// <summary>
    /// Wrapping status-change indicator, independent of packet sequences and ticking ages.
    /// </summary>
    private uint revision = 1;

    /// <summary>
    /// Status revision most recently emitted to the PC.
    /// </summary>
    private uint lastSentRevision;

    /// <summary>
    /// Saturating count of inactive-to-active simulated depth faults in this boot.
    /// </summary>
    private uint depthFaultOccurrences;

    /// <summary>
    /// Onboard time of the most recent depth-fault event or repeat publication.
    /// </summary>
    private uint lastFault;

    /// <summary>
    /// Whether the last emitted depth-fault event indicated an active fault.
    /// </summary>
    private bool reportedDepthFault;

    /// <summary>
    /// Previously reported arming-blocker mask used to detect qualification changes.
    /// </summary>
    private ArmingBlockers previousBlockers;

    /// <summary>
    /// Current uninterrupted neutral qualification in milliseconds, capped at one second.
    /// </summary>
    private ushort neutralProgress;

    /// <summary>
    /// Provisioned identity advertised by this simulated vehicle.
    /// </summary>
    public OpaqueId128 VehicleId { get; } = DefaultVehicleId;

    /// <summary>
    /// Current authoritative simulated safety state; valid wire syntax alone cannot change it.
    /// </summary>
    public SafetyState State { get; private set; } = SafetyState.Disarmed;

    /// <summary>
    /// Movement source selected by the simulated physical ownership selector.
    /// </summary>
    public MovementOwner Owner { get; private set; } = MovementOwner.Pc;

    /// <summary>
    /// Current authorization generation used to invalidate tickets and earlier enabling requests.
    /// </summary>
    public uint Generation { get; private set; } = 1;

    /// <summary>
    /// Event that entered the currently reported safety state.
    /// </summary>
    public SafetyCause Cause { get; private set; } = SafetyCause.Startup;

    /// <summary>
    /// Current simulated depth below the water surface in millimetres.
    /// </summary>
    public int DepthMm { get; private set; } = 2000;

    /// <summary>
    /// Whether an established PC session currently exists.
    /// </summary>
    public bool HasSession => established;

    /// <summary>
    /// Current established session identity, independent of arming permission.
    /// </summary>
    public SessionId Session => session;

    /// <summary>
    /// Fault-injection switch that suppresses outgoing tickets without renewing existing authorization.
    /// </summary>
    public bool SuppressTickets
    {
        get; set;
    }

    /// <summary>
    /// Fault-injection switch that suppresses status so the desktop must report unconfirmed state.
    /// </summary>
    public bool SuppressStatus
    {
        get; set;
    }

    /// <summary>
    /// Fault-injection switch that suppresses measurement transmission without refreshing old samples.
    /// </summary>
    public bool SuppressTelemetry
    {
        get; set;
    }

    /// <summary>
    /// Fault-injection switch that drops movement commands while other peer traffic can continue.
    /// </summary>
    public bool DropIncomingMovement
    {
        get; set;
    }

    /// <summary>
    /// Whether the simulated depth source is healthy enough for its required control checks.
    /// </summary>
    public bool DepthHealthy => depthHealthy;

    /// <summary>
    /// Effective forward effort, or zero when movement is not authorized.
    /// </summary>
    public short ThrustEffort => State == SafetyState.Armed && hasMovement ? forward : (short)0;

    /// <summary>
    /// Effective requested turning effort while movement is authorized.
    /// </summary>
    public short YawEffort => State == SafetyState.Armed && hasMovement ? yaw : (short)0;

    /// <summary>
    /// Effective dive effort after arming and downward-control inhibition checks.
    /// </summary>
    public short DiveEffort => State == SafetyState.Armed && hasMovement && (!restricted || dive <= 0) ? dive : (short)0;

    /// <summary>
    /// Effective buoyancy action after safety overrides and renewable-target expiry.
    /// </summary>
    public BuoyancyAction Buoyancy => State == SafetyState.EmergencyStopLatched ? BuoyancyAction.EmergencyEvacuation : State is SafetyState.CommandLossFailsafe or SafetyState.FaultFailsafe || State == SafetyState.Armed && (!hasBuoyancy || restricted && target == BuoyancyTarget.Negative) ? BuoyancyAction.SeekNeutral : State != SafetyState.Armed ? BuoyancyAction.None : target switch
    {
        BuoyancyTarget.Positive => BuoyancyAction.SeekPositive,
        BuoyancyTarget.Negative => BuoyancyAction.SeekNegative,
        _ => BuoyancyAction.SeekNeutral
    };

    /// <summary>
    /// Computes elapsed onboard milliseconds using wrap-safe unsigned subtraction.
    /// </summary>
    private static uint Age(uint now, uint then) => unchecked(now - then);

    /// <summary>
    /// Checks the independent receipt and issuing-ticket deadlines of the accepted movement intent.
    /// </summary>
    private bool MovementFresh(uint now) => hasMovement && Age(now, movementAt) < 500 && Age(now, movementTicketAt) < 500;

    /// <summary>
    /// Checks the independent receipt and issuing-ticket deadlines of the normal buoyancy target.
    /// </summary>
    private bool BuoyancyFresh(uint now) => hasBuoyancy && Age(now, buoyancyAt) < 500 && Age(now, buoyancyTicketAt) < 500;

    /// <summary>
    /// Changes the simulated physical source selector, disarming active control while preserving safety latches.
    /// </summary>
    public void SetOwner(MovementOwner owner, uint now)
    {
        if (!Enum.IsDefined(owner))
        {
            throw new ArgumentOutOfRangeException(nameof(owner));
        }

        if (Owner == owner)
        {
            return;
        }

        Owner = owner;
        if (State == SafetyState.Armed)
        {
            State = SafetyState.Disarmed;
            Cause = SafetyCause.OwnerSelectorChanged;
        }

        ChangeGeneration();
        revision++;
        Safety(now);
    }

    /// <summary>
    /// Injects depth and sensor health and applies the simulated depth-limit and sensor-loss interlocks.
    /// </summary>
    public void SetDepth(int millimetres, bool healthy, uint now)
    {
        if (millimetres < 0)
        {
            throw new ArgumentOutOfRangeException(nameof(millimetres));
        }

        DepthMm = millimetres;
        depthMetres = millimetres / 1000d;
        if (!healthy && depthHealthy && depthFaultOccurrences < uint.MaxValue)
        {
            depthFaultOccurrences++;
        }

        depthHealthy = healthy;
        if (!healthy && State is SafetyState.Armed or SafetyState.CommandLossFailsafe)
        {
            Enter(SafetyState.FaultFailsafe, SafetyCause.DepthUnavailable);
        }

        // Simulation-only profile: intervene at 4.5 m, release below 4 m. Not hardware calibration.
        if (millimetres >= 4500 && !restricted)
        {
            restricted = true;
            ChangeGeneration(preserveMovement: true);
            hasBuoyancy = false;
            revision++;
        }

        Safety(now);
    }

    /// <summary>
    /// Clears the simulated emergency latch locally and returns to disarmed qualification.
    /// </summary>
    public bool LocalEmergencyReset(uint now)
    {
        if (State != SafetyState.EmergencyStopLatched)
        {
            return false;
        }

        // The simulated procedure completes immediately; real hardware needs independent completion evidence.
        emergencyLatch = false;
        State = SafetyState.Disarmed;
        Cause = SafetyCause.LocalEmergencyStopReset;
        ChangeGeneration();
        Safety(now);
        return true;
    }

    /// <summary>
    /// Starts a new simulated boot identity while preserving an active in-memory emergency latch.
    /// </summary>
    public void Reboot(uint now)
    {
        if (boot == uint.MaxValue)
        {
            throw CounterFault("Boot identity exhausted; service required.");
        }

        boot++;
        sessionCounter = 0;
        established = pending = false;
        results.Clear();
        resultOrder.Clear();
        highestRequest = 0;
        State = emergencyLatch ? SafetyState.EmergencyStopLatched : SafetyState.Disarmed;
        Cause = emergencyLatch ? SafetyCause.EmergencyStopAsserted : SafetyCause.Startup;
        ChangeGeneration();
        Safety(now);
    }

    /// <summary>
    /// Enters a higher-priority safety state and invalidates previous control authorization.
    /// </summary>
    private void Enter(SafetyState state, SafetyCause cause)
    {
        if (State == SafetyState.EmergencyStopLatched || State == state)
        {
            return;
        }

        if (State == SafetyState.FaultFailsafe && state == SafetyState.CommandLossFailsafe)
        {
            return;
        }

        State = state;
        Cause = cause;
        emergencyLatch |= state == SafetyState.EmergencyStopLatched;
        ChangeGeneration();
        revision++;
    }

    /// <summary>
    /// Invalidates tickets and prior qualification, optionally preserving permitted movement only until its existing deadline.
    /// </summary>
    private void ChangeGeneration(bool preserveMovement = false)
    {
        if (Generation == uint.MaxValue)
        {
            throw CounterFault("Control counter exhausted.");
        }

        Generation++;
        acceptedInGeneration = false;
        tickets.Clear();
        ticketOrder.Clear();
        neutralSince = releaseSince = null;
        hasBuoyancy = false;
        if (!preserveMovement)
        {
            hasMovement = false;
            forward = yaw = dive = 0;
        }

        revision++;
    }

    /// <summary>
    /// Applies deadline expiry, neutral qualification and restricted-depth release conditions at the supplied onboard time.
    /// </summary>
    private void Safety(uint now)
    {
        if (pending && Age(now, pendingSince) >= 2000)
        {
            pending = false;
        }

        bool fresh = MovementFresh(now);
        if (State == SafetyState.Armed && (!depthHealthy))
        {
            Enter(SafetyState.FaultFailsafe, SafetyCause.DepthUnavailable);
        }

        if (State == SafetyState.Armed && ((!fresh && hasMovement) || (!hasMovement && Age(now, armedSince) >= 500)))
        {
            Enter(SafetyState.CommandLossFailsafe, SafetyCause.MovementExpired);
        }

        if (!fresh)
        {
            neutralSince = null;
            hasMovement = false;
        }

        if (!BuoyancyFresh(now))
        {
            hasBuoyancy = false;
        }

        neutralProgress = neutralSince is uint start && fresh ? (ushort)Math.Min(1000u, Age(now, start)) : (ushort)0;
        if (restricted && State == SafetyState.Armed && depthHealthy && DepthMm < 4000 && fresh && hasBuoyancy && target == BuoyancyTarget.Neutral && dive <= 0)
        {
            releaseSince ??= now;
            if (Age(now, releaseSince.Value) >= 1000)
            {
                restricted = false;
                ChangeGeneration(preserveMovement: true);
                revision++;
            }
        }
        else
        {
            releaseSince = null;
        }
    }

    /// <summary>
    /// Validates ticket identity, generation and onboard issuance age before a command can be accepted.
    /// </summary>
    private bool TryTicket(uint id, uint generation, uint now, out uint issued, out CommandResultCode failure)
    {
        issued = 0;
        if (generation != Generation)
        {
            failure = CommandResultCode.WrongControlGeneration;
            return false;
        }

        if (!tickets.TryGetValue(id, out issued) || Age(now, issued) >= 500)
        {
            failure = CommandResultCode.UnknownOrExpiredTicket;
            return false;
        }

        failure = CommandResultCode.Accepted;
        return true;
    }

    /// <summary>
    /// Validates a client packet and applies handshake, continuous-command or discrete-request rules at onboard time.
    /// </summary>
    public IReadOnlyList<BcpPacket> Receive(BcpPacket packet, uint now)
    {
        Safety(now);
        Span<byte> validation = stackalloc byte[116];
        if (!BcpCodec.TryEncode(packet, WireDirection.PcToVehicle, validation, out _, out _))
        {
            return [];
        }

        var output = new List<BcpPacket>(5);
        if (packet.Message is Hello hello)
        {
            if (packet.Session != default || packet.Sequence != 0 || hello.RequestedVersion != 1)
            {
                return output;
            }

            if (established && hello.Nonce == nonce)
            {
                output.Add(Emit(new HelloReply(nonce, 1, VehicleId)));
                return output;
            }

            if (pending)
            {
                if (hello.Nonce == pendingNonce)
                {
                    output.Add(PendingReply());
                }

                return output;
            }

            if (State == SafetyState.Armed && Owner == MovementOwner.Pc)
            {
                return output;
            }

            if (sessionCounter == uint.MaxValue)
            {
                throw CounterFault("Session counter exhausted.");
            }

            pending = true;
            pendingSince = now;
            pendingNonce = hello.Nonce;
            pendingSession = new(boot, ++sessionCounter);
            pendingOutbound = 0;
            output.Add(PendingReply());
            return output;
        }

        if (packet.Message is SessionConfirm confirm)
        {
            if (established && packet.Session == session && confirm.Nonce == nonce)
            {
                output.Add(Emit(new SessionReady(Generation)));
                return output;
            }

            if (!pending || packet.Session != pendingSession || confirm.Nonce != pendingNonce || packet.Sequence == 0 || State == SafetyState.Armed && Owner == MovementOwner.Pc)
            {
                return output;
            }

            session = pendingSession;
            nonce = pendingNonce;
            outbound = pendingOutbound;
            established = true;
            pending = false;
            highestRequest = movementSequence = buoyancySequence = ticketId = 0;
            results.Clear();
            resultOrder.Clear();
            tickets.Clear();
            ticketOrder.Clear();
            if (Owner == MovementOwner.Pc)
            {
                hasMovement = hasBuoyancy = false;
                neutralSince = null;
            }

            output.Add(Emit(new SessionReady(Generation)));
            output.Add(Emit(GetCapabilities()));
            output.Add(Emit(Status(now)));
            return output;
        }

        if (!established || packet.Session != session || packet.Sequence == 0)
        {
            return output;
        }

        uint before = revision;
        switch (packet.Message)
        {
            case MovementCommand m when !DropIncomingMovement:
                if (Owner != MovementOwner.Pc || packet.Sequence <= movementSequence || State == SafetyState.EmergencyStopLatched || restricted && m.Dive > 0 || !TryTicket(m.TicketId, m.ControlGeneration, now, out uint issued, out _))
                {
                    break;
                }

                movementSequence = packet.Sequence;
                hasMovement = acceptedInGeneration = true;
                movementAt = now;
                movementTicketAt = issued;
                forward = m.Forward;
                yaw = m.Yaw;
                dive = m.Dive;
                if (Math.Abs((int)forward) <= 50 && Math.Abs((int)yaw) <= 50 && Math.Abs((int)dive) <= 50)
                {
                    neutralSince ??= now;
                }
                else
                {
                    neutralSince = null;
                }

                break;
            case BuoyancyCommand b:
                if (State != SafetyState.Armed || Owner != MovementOwner.Pc || packet.Sequence <= buoyancySequence || restricted && b.Target == BuoyancyTarget.Negative || !TryTicket(b.TicketId, b.ControlGeneration, now, out uint bIssued, out _))
                {
                    break;
                }

                buoyancySequence = packet.Sequence;
                hasBuoyancy = true;
                buoyancyAt = now;
                buoyancyTicketAt = bIssued;
                target = b.Target;
                break;
            case Arm or Disarm or EmergencyStop or ClearFailsafe:
                output.Add(Emit(Discrete(packet.Message, now)));
                break;
            case Bajols.Protocol.GetCapabilities:
                output.Add(Emit(GetCapabilities()));
                break;
        }

        Safety(now);
        if (before != revision && !SuppressStatus)
        {
            output.Add(Emit(Status(now)));
        }

        return output;
    }

    /// <summary>
    /// Builds the next reply for the existing pending handshake without replacing its identity or deadline.
    /// </summary>
    private BcpPacket PendingReply()
    {
        if (pendingOutbound == uint.MaxValue)
        {
            throw CounterFault("Pending counter exhausted.");
        }

        return new(pendingSession, ++pendingOutbound, new HelloReply(pendingNonce, 1, VehicleId));
    }

    /// <summary>
    /// Applies bounded request deduplication and stateful authorization to explicit control actions.
    /// </summary>
    private CommandResult Discrete(BcpMessage message, uint now)
    {
        uint id = message switch
        {
            Arm m => m.RequestId,
            Disarm m => m.RequestId,
            EmergencyStop m => m.RequestId,
            ClearFailsafe m => m.RequestId,
            _ => 0
        };
        if (results.TryGetValue(id, out var previous))
        {
            return previous.Message == message ? previous.Result : new(id, message.Type, CommandResultCode.RequestIdReused, Generation);
        }

        if (id == 0 || id <= highestRequest)
        {
            return new(id, message.Type, CommandResultCode.RequestTooOld, Generation);
        }

        highestRequest = id;
        var code = CommandResultCode.Accepted;
        if (message is EmergencyStop)
        {
            Enter(SafetyState.EmergencyStopLatched, SafetyCause.EmergencyStopAsserted);
        }
        else
        {
            if (message is Disarm)
            {
                if (State == SafetyState.Armed)
                {
                    State = SafetyState.Disarmed;
                    Cause = SafetyCause.ExplicitDisarm;
                    ChangeGeneration();
                }
            }
            else
            {
                if (State == SafetyState.EmergencyStopLatched)
                {
                    code = CommandResultCode.LocalResetRequired;
                }
                else
                {
                    if (Owner != MovementOwner.Pc)
                    {
                        code = CommandResultCode.NotMovementOwner;
                    }
                    else
                    {
                        uint ticket = message is Arm a ? a.TicketId : ((ClearFailsafe)message).TicketId;
                        uint generation = message is Arm ar ? ar.ControlGeneration : ((ClearFailsafe)message).ControlGeneration;
                        if (!TryTicket(ticket, generation, now, out _, out code))
                        {
                        }
                        else
                        {
                            if (!depthHealthy)
                            {
                                code = CommandResultCode.BlockingFault;
                            }
                            else
                            {
                                if (!MovementFresh(now))
                                {
                                    code = CommandResultCode.NeutralHoldIncomplete;
                                }
                                else
                                {
                                    if (message is Arm)
                                    {
                                        if (State != SafetyState.Disarmed)
                                        {
                                            code = CommandResultCode.InvalidSafetyState;
                                        }
                                        else
                                        {
                                            if (!MovementFresh(now) || neutralProgress < 1000)
                                            {
                                                code = CommandResultCode.NeutralHoldIncomplete;
                                            }
                                            else
                                            {
                                                State = SafetyState.Armed;
                                                Cause = SafetyCause.ExplicitArm;
                                                armedSince = now;
                                                ChangeGeneration();
                                            }
                                        }
                                    }
                                    else
                                    {
                                        if (State is not (SafetyState.CommandLossFailsafe or SafetyState.FaultFailsafe))
                                        {
                                            code = CommandResultCode.InvalidSafetyState;
                                        }
                                        else
                                        {
                                            Cause = State == SafetyState.FaultFailsafe ? SafetyCause.FaultFailsafeCleared : SafetyCause.CommandLossFailsafeCleared;
                                            State = SafetyState.Disarmed;
                                            ChangeGeneration();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        var result = new CommandResult(id, message.Type, code, Generation);
        if (results.Count == 16)
        {
            results.Remove(resultOrder.Dequeue());
        }

        results[id] = (message, result);
        resultOrder.Enqueue(id);
        return result;
    }

    /// <summary>
    /// Advances safety and illustrative motion and emits bounded scheduled tickets, status, faults and telemetry.
    /// </summary>
    public IReadOnlyList<BcpPacket> Tick(uint now)
    {
        Safety(now);
        AdvanceMotion(now);
        var output = new List<BcpPacket>(5);
        if (!established)
        {
            return output;
        }

        if (Age(now, lastTicket) >= 50 && !SuppressTickets)
        {
            lastTicket = now;
            if (ticketId == uint.MaxValue)
            {
                Enter(SafetyState.FaultFailsafe, SafetyCause.OtherBlockingFault);
                established = false;
                return output;
            }

            uint id = ++ticketId;
            if (tickets.Count == 16)
            {
                tickets.Remove(ticketOrder.Dequeue());
            }

            tickets[id] = now;
            ticketOrder.Enqueue(id);
            output.Add(Emit(new ControlTicket(id, Generation, 500)));
        }

        var currentStatus = Status(now);
        if ((Age(now, lastStatus) >= 200 || currentStatus.StatusRevision != lastSentRevision) && !SuppressStatus)
        {
            lastStatus = now;
            output.Add(Emit(currentStatus));
        }

        bool depthFault = !depthHealthy;
        if (depthFault != reportedDepthFault || depthFault && Age(now, lastFault) >= 1000)
        {
            lastFault = now;
            reportedDepthFault = depthFault;
            output.Add(Emit(new Fault(FaultCode.DepthUnavailable, FaultSeverity.Blocking, depthFault, depthFaultOccurrences)));
        }

        if (Age(now, lastTelemetry) >= 200 && !SuppressTelemetry)
        {
            lastTelemetry = now;
            var quality = TelemetryQuality.Valid | TelemetryQuality.Simulated;
            output.Add(Emit(new Telemetry(new TelemetryRecord[] { new(MeasurementId.Depth, depthHealthy ? quality : TelemetryQuality.SensorFault | TelemetryQuality.Simulated, 0, DepthMm), new(MeasurementId.SupplyVoltage, quality, 0, 12000), new(MeasurementId.Heading, quality, 0, (int)Math.Round(HeadingDegrees * 100) % 36000), new(MeasurementId.Pitch, quality, 0, (int)Math.Round(PitchDegrees * 100)), new(MeasurementId.PropulsionRpm, quality, 0, Math.Abs(ThrustEffort) * 3) })));
        }

        if (Age(now, lastHeartbeat) >= 1000)
        {
            lastHeartbeat = now;
            output.Add(Emit(new Heartbeat()));
        }

        return output;
    }

    /// <summary>
    /// Integrates illustrative thrust-dependent heading, pitch and depth while retaining the existing safety interlocks.
    /// </summary>
     // Illustrative kinematics only: not hydrodynamics or a validated ballast controller.
    private void AdvanceMotion(uint now)
    {
        double dt = lastMotionAt is uint previous ? Math.Min(0.1, Age(now, previous) / 1000d) : 0;
        lastMotionAt = now;
        double targetPitch = -DiveEffort / 1000d * 25;
        PitchDegrees += (targetPitch - PitchDegrees) * (1 - Math.Exp(-dt * 4));
        double steeringAuthority = Math.Abs(ThrustEffort) / 1000d;
        HeadingDegrees = (HeadingDegrees + YawEffort / 1000d * steeringAuthority * 30 * dt + 360) % 360;
        double ballastVelocity = Buoyancy switch
        {
            BuoyancyAction.SeekPositive => -0.15,
            BuoyancyAction.SeekNegative => 0.15,
            BuoyancyAction.EmergencyEvacuation => -0.7,
            _ => 0
        };
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

    /// <summary>
    /// Stops simulated control on authorization-counter exhaustion without downgrading an emergency latch.
    /// </summary>
    private InvalidOperationException CounterFault(string message)
    {
        if (State != SafetyState.EmergencyStopLatched)
        {
            State = SafetyState.FaultFailsafe;
        }

        hasMovement = hasBuoyancy = established = false;
        return new InvalidOperationException(message);
    }

    /// <summary>
    /// Assigns an outbound sequence and tracks the revision of transmitted status.
    /// </summary>
    private BcpPacket Emit(BcpMessage message)
    {
        if (message is VehicleStatus sent)
        {
            lastSentRevision = sent.StatusRevision;
        }

        if (outbound == uint.MaxValue)
        {
            throw CounterFault("Packet counter exhausted.");
        }

        return new(session, ++outbound, message);
    }

    /// <summary>
    /// Describes only the control functions and measurements implemented by this simulated peer.
    /// </summary>
    public static Capabilities GetCapabilities() => new(1, 1, 7, 15, 0x3d, 50, 500, 50, 500, 1000, 50);

    /// <summary>
    /// Builds a current safety snapshot with independent freshness, blocker and buoyancy-action reporting.
    /// </summary>
    public VehicleStatus Status(uint now)
    {
        Safety(now);
        ArmingBlockers blockers = ArmingBlockers.None;
        if (Owner == MovementOwner.None)
        {
            blockers |= ArmingBlockers.NoOwner;
        }

        if (!MovementFresh(now))
        {
            blockers |= ArmingBlockers.StaleOwner;
        }

        if (hasMovement && (Math.Abs((int)forward) > 50 || Math.Abs((int)yaw) > 50 || Math.Abs((int)dive) > 50))
        {
            blockers |= ArmingBlockers.NonNeutralInput;
        }

        if (neutralProgress < 1000)
        {
            blockers |= ArmingBlockers.NeutralHoldIncomplete;
        }

        if (!depthHealthy)
        {
            blockers |= ArmingBlockers.BlockingFault | ArmingBlockers.RequiredSubsystemUnavailable;
        }

        blockers |= State switch
        {
            SafetyState.CommandLossFailsafe => ArmingBlockers.CommandLossLatch,
            SafetyState.FaultFailsafe => ArmingBlockers.FaultFailsafeLatch,
            SafetyState.EmergencyStopLatched => ArmingBlockers.EmergencyLatch,
            _ => ArmingBlockers.None
        };
        if (blockers != previousBlockers)
        {
            revision++;
            previousBlockers = blockers;
        }

        ushort remaining = MovementFresh(now) ? (ushort)Math.Min(500 - Age(now, movementAt), 500 - Age(now, movementTicketAt)) : (ushort)0;
        ushort bRemaining = BuoyancyFresh(now) ? (ushort)Math.Min(500 - Age(now, buoyancyAt), 500 - Age(now, buoyancyTicketAt)) : (ushort)0;
        ActiveRestrictions restrictions = restricted ? ActiveRestrictions.DepthLimitActive | ActiveRestrictions.DownwardControlInhibited : ActiveRestrictions.None;
        if (!depthHealthy)
        {
            restrictions |= ActiveRestrictions.DepthUnavailable | ActiveRestrictions.DownwardControlInhibited;
        }

        var snapshot = new VehicleStatus(revision, now, State, Owner, ControlMode.Effort, Generation, blockers, neutralProgress, acceptedInGeneration ? (ushort)Math.Min(65534u, Age(now, movementAt)) : ushort.MaxValue, remaining, Buoyancy, !depthHealthy && Buoyancy == BuoyancyAction.SeekNeutral ? ActionStatus.Unavailable : Buoyancy == BuoyancyAction.None ? ActionStatus.Inactive : State == SafetyState.EmergencyStopLatched ? ActionStatus.Completed : ActionStatus.Running, Cause, EstimatedBuoyancy.Unknown, !hasBuoyancy ? AuthorizedBuoyancyTarget.None : target switch
        {
            BuoyancyTarget.Positive => AuthorizedBuoyancyTarget.Positive,
            BuoyancyTarget.Negative => AuthorizedBuoyancyTarget.Negative,
            _ => AuthorizedBuoyancyTarget.Neutral
        }, bRemaining, restrictions);
        var shape = snapshot with
        {
            StatusRevision = 0,
            VehicleUptimeMs = 0,
            NeutralHoldProgressMs = 0,
            MovementAgeMs = 0,
            MovementTimeRemainingMs = 0,
            BuoyancyCommandTimeRemainingMs = 0
        };
        if (shape != previousStatusShape)
        {
            revision++;
            previousStatusShape = shape;
        }

        return snapshot with
        {
            StatusRevision = revision
        };
    }
}
