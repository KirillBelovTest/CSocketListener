(* ::Package:: *)

PacletObject[
  <|
    "Name" -> "KirillBelov/CSockets",
    "Description" -> "Sockets powered by pure C",
    "Creator" -> "Kirill Belov",
    "License" -> "MIT",
    "PublisherID" -> "KirillBelov",
    "Version" -> "1.0.8",
    "WolframVersion" -> "12+",
    "PrimaryContext" -> "KirillBelov`CSockets`",
    "Extensions" -> {
      {
        "Kernel",
        "Root" -> "Kernel",
        "Context" -> {"KirillBelov`CSockets`"},
        "Symbols" -> {
          "KirillBelov`CSockets`CSocketObject",
          "KirillBelov`CSockets`CSocketListener",
          "KirillBelov`CSockets`CSocketOpen",
          "KirillBelov`CSockets`CSocketConnect"
        }
      },
      {"Documentation", "Language" -> "English"},
      {"LibraryLink", "Root" -> "LibraryResources"},
      {
        "Asset",
        "Assets" -> {
          {"License", "./LICENSE"},
          {"ReadMe", "./README.md"},
          {"Source", "./Source"},
          {"Scripts", "./Scripts"},
          {"Tests", "./Tests"},
          {"Images", "./Images"}
        }
      }
    }
  |>
]
