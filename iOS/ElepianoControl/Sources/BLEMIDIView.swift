import SwiftUI
import CoreAudioKit

/// iOS 標準の BLE-MIDI ペアリング画面を UIViewControllerRepresentable でラップ
struct BLEMIDIView: UIViewControllerRepresentable {
    func makeUIViewController(context: Context) -> CABTMIDICentralViewController {
        CABTMIDICentralViewController()
    }

    func updateUIViewController(_ vc: CABTMIDICentralViewController, context: Context) {}
}
