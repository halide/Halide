open System.IO
open System.Text.RegularExpressions

// turn Regex.Replace into a function
let makeRegexReplaceFunc (pattern: string) (replacement: string) =

    fun str -> Regex.Replace( str, pattern, replacement )

let main (args : string[]) =

    if args.Length > 0 then

        let inputProjFile = args.[1]

        let outputProjFile =
            if args.Length > 2 then
                args.[2]
            else
                inputProjFile
    
        printfn "Converting Win32 project to x64"
        printfn "Input: %A" inputProjFile
        printfn "Output %A" outputProjFile

        let debugReplaceFunc = makeRegexReplaceFunc "Debug\|Win32" "Debug|x64"
        let releaseReplaceFunc = makeRegexReplaceFunc "Release\|Win32" "Release|x64"
        let releasePlatformReplaceFunc = makeRegexReplaceFunc "<Platform>Win32</Platform>" "<Platform>x64</Platform>"
        let rootNamespaceReplaceFunc = makeRegexReplaceFunc "<RootNamespace>(.*)d</RootNamespace>" "<RootNamespace>$1</RootNamespace>"

        let inputLines = File.ReadAllLines inputProjFile
    
        let outputLines =
            inputLines
            |> Seq.map debugReplaceFunc
            |> (Seq.map releaseReplaceFunc)
            |> (Seq.map releasePlatformReplaceFunc)
            |> Seq.map rootNamespaceReplaceFunc

        File.WriteAllLines( outputProjFile, outputLines )
    
        0

    else
        printfn "Usage: %s <input.vcxproj> <output.vcxproj>" args.[0]

        1

main fsi.CommandLineArgs
