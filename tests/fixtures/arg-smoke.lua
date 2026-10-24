-- Copyright 2026 Jamison A. Drapeau
-- No network operations: exercise the real Kaminowaku Lua interpreter.
book {
        name = "arg-smoke"
}

assert(type(args) == "table")
if args.mode == nil then
        -- A second run must not inherit the previous target/run's args.
        assert(args.port == nil)
        assert(args.path == nil)
        assert(args["custom-name"] == nil)
else
        assert(type(args.port) == "string")
        assert(args.port == "8443")
        assert(args.path == "/health check")
        assert(args["custom-name"] == "alpha beta")
        assert(args.mode == "deep")
end
