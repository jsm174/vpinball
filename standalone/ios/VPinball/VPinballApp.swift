import Foundation
import SwiftUI

@_silgen_name("vpinball_swift_startup")
func vpinballSwiftStartup(window: UnsafeMutableRawPointer?) {
    DispatchQueue.main.async {
        guard let rootViewController = UIApplication.shared.windows.first?.rootViewController else {
            return
        }

        UISearchBar.appearance().overrideUserInterfaceStyle = .dark

        let overlayView = VPinballAppView()
            .environmentObject(VPinballViewModel.shared)

        let hostingController = UIHostingController(rootView: overlayView)
        hostingController.view.backgroundColor = UIColor.clear // Transparent background

        rootViewController.addChild(hostingController)
        rootViewController.view.addSubview(hostingController.view)
        hostingController.didMove(toParent: rootViewController)

        hostingController.view.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            hostingController.view.topAnchor.constraint(equalTo: rootViewController.view.topAnchor),
            hostingController.view.bottomAnchor.constraint(equalTo: rootViewController.view.bottomAnchor),
            hostingController.view.leadingAnchor.constraint(equalTo: rootViewController.view.leadingAnchor),
            hostingController.view.trailingAnchor.constraint(equalTo: rootViewController.view.trailingAnchor)
        ])
    }
}

struct VPinballAppView: View {
    @State var showSplash = true

    var body: some View {
        if showSplash {
            SplashView()
                .onAppear {
                    handleAppear()
                }
        } else {
            MainView()
        }
    }

    func handleAppear() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
            withAnimation {
                showSplash = false
            }
        }
    }
}
