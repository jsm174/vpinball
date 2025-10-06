import Foundation
import SwiftUI

struct VPinballAppView: View {
    @ObservedObject var vpinballViewModel = VPinballViewModel.shared

    @State var showSplash = true
    @State var showMainView = true

    var body: some View {
        ZStack {
            Color.clear

            if showSplash {
                SplashView()
                    .onAppear {
                        handleAppear()
                    }
            } else {
                MainView()
                    .opacity(showMainView ? 1 : 0)
            }
        }
        .onChange(of: vpinballViewModel.isPlaying) {
            showMainView = !vpinballViewModel.isPlaying

            StatusBarManager.shared.setHidden(!showMainView,
                                              animated: false)
        }
    }

    func handleAppear() {
        VPinballManager.shared.startup()

        DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
            withAnimation {
                showSplash = false
            }
        }
    }
}
