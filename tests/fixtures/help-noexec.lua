-- Only the book declaration is read for --help.
book {
        name = "help-noexec",
        description = "An inert metadata-only integration test.",
        options = {
                ["custom-name"] = "A custom author-defined parameter."
        }
}
error("The help loader must never execute Book code")
