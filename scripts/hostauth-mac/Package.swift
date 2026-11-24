// swift-tools-version:5.9
// Mac side of Flipware host sign-in. See documentation/HostAuth.md.
import PackageDescription

let package = Package(
    name: "flipware-auth",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "flipware-auth",
            path: "Sources/flipware-auth",
            linkerSettings: [
                .linkedFramework("CryptoKit"),
                .linkedFramework("LocalAuthentication"),
                .linkedFramework("Security"),
            ]
        )
    ]
)
