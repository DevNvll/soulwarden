// Disable expensive analyzers for the first pass over the 133 MB client code section.
// Function starts come from the exception directory. Switch recovery and constant
// propagation run later on selected functions only.
// @category Soulwarden

import java.util.Map;

import ghidra.app.script.GhidraScript;

public final class ConfigureFastAnalysis extends GhidraScript {
    private static final String[] DISABLED = {
        "Aggressive Instruction Finder",
        "Apply Data Archives",
        "Call Convention ID",
        "Create Address Tables",
        "Decompiler Parameter ID",
        "Decompiler Switch Analysis",
        "Embedded Media",
        "Function ID",
        "Non-Returning Functions - Discovered",
        "PDB MSDIA",
        "PDB Universal",
        "Scalar Operand References",
        "Stack",
        "Variadic Function Signature Override",
        "WindowsResourceReference",
        "x86 Constant Reference Analyzer",
    };

    @Override
    public void run() throws Exception {
        Map<String, String> options = getCurrentAnalysisOptionsAndValues(currentProgram);
        for (String name : DISABLED) {
            if (options.containsKey(name)) {
                setAnalysisOption(currentProgram, name, "false");
                println("DISABLED " + name);
            }
        }
    }
}
