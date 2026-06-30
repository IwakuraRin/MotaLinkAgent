namespace MotaBridge.Protocol;

public static class BridgeMessageTypes
{
    public const string SessionCreate = "session.create";
    public const string SessionInput = "session.input";
    public const string SessionSignal = "session.signal";
    public const string SessionResize = "session.resize";
    public const string SessionClose = "session.close";
    public const string SessionList = "session.list";

    public const string Ready = "bridge.ready";
    public const string Error = "error";
    public const string SessionCreated = "session.created";
    public const string SessionOutput = "session.output";
    public const string SessionExit = "session.exit";
    public const string SessionClosed = "session.closed";
    public const string SessionListResult = "session.list.result";
}
