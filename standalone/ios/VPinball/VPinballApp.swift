import Foundation
import SwiftUI

@_silgen_name("vpinballBridgeStartup")
func vpinballBridgeStartup(window: UnsafeMutableRawPointer?) {
    DispatchQueue.main.async {
        guard let raw = window,
              let uiWindow = Unmanaged<AnyObject>.fromOpaque(raw).takeUnretainedValue() as? UIWindow,
              let rootViewController = uiWindow.rootViewController else { return }

        StatusBarManager.install(on: rootViewController,
                                 hidden: false,
                                 style: .lightContent,
                                 animated: false)

        UISearchBar.appearance().overrideUserInterfaceStyle = .dark

        rootViewController.view.isMultipleTouchEnabled = true

        let hostingController = UIHostingController(rootView: VPinballAppView()
            .environmentObject(VPinballViewModel.shared))

        hostingController.view.backgroundColor = UIColor.clear
        hostingController.view.isMultipleTouchEnabled = true

        rootViewController.addChild(hostingController)
        rootViewController.view.addSubview(hostingController.view)
        hostingController.didMove(toParent: rootViewController)

        hostingController.view.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            hostingController.view.topAnchor.constraint(equalTo: rootViewController.view.topAnchor),
            hostingController.view.bottomAnchor.constraint(equalTo: rootViewController.view.bottomAnchor),
            hostingController.view.leadingAnchor.constraint(equalTo: rootViewController.view.leadingAnchor),
            hostingController.view.trailingAnchor.constraint(equalTo: rootViewController.view.trailingAnchor),
        ])
    }
}

struct VPinballAppView: View {
    @EnvironmentObject var vpinballViewModel: VPinballViewModel
    @State var showSplash = true
    @State var showMainView = true

    var body: some View {
        ZStack {
            Color.clear

            if showMainView {
                if showSplash {
                    SplashView()
                        .onAppear {
                            handleAppear()
                        }
                } else {
                    MainView()
                }
            }

            // OverlayView()
            //  .allowsHitTesting(vpinballViewModel.isPlaying)
        }
        .onChange(of: vpinballViewModel.isPlaying) { isPlaying in
            showMainView = !isPlaying

            StatusBarManager.shared.setHidden(!showMainView,
                                              animated: false)
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
