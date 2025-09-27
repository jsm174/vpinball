import ObjectiveC.runtime
import SwiftUI
import UIKit

public final class StatusBarManager {
    public static let shared = StatusBarManager()

    private weak var attachedRootViewController: UIViewController?
    private weak var owningViewController: UIViewController?
    private var hidden = false
    private var style: UIStatusBarStyle = .default
    private var animated = true

    private static var kInstalledKey: UInt8 = 0
    private static var kHiddenKey: UInt8 = 0
    private static var kStyleKey: UInt8 = 0

    public func attach(to root: UIViewController) {
        attachedRootViewController = root
        let top = topMost(from: root)
        owningViewController = top
        swizzleIfNeeded(on: top)
    }

    public func setHidden(_ hidden: Bool, style: UIStatusBarStyle? = nil, animated: Bool = true) {
        guard let root = attachedRootViewController else { return }
        self.hidden = hidden
        if let s = style { self.style = s }
        self.animated = animated
        ensureAttached()
        guard let vc = owningViewController else { return }
        objc_setAssociatedObject(vc, &Self.kHiddenKey, NSNumber(value: self.hidden), .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
        objc_setAssociatedObject(vc, &Self.kStyleKey, NSNumber(value: self.style.rawValue), .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
        if animated {
            UIView.animate(withDuration: 0.25) { vc.setNeedsStatusBarAppearanceUpdate() }
        } else {
            vc.setNeedsStatusBarAppearanceUpdate()
        }
    }

    public func reattachToTopMost() {
        guard let root = attachedRootViewController else { return }
        let top = topMost(from: root)
        if top !== owningViewController {
            owningViewController = top
            swizzleIfNeeded(on: top)
            setHidden(hidden, style: style, animated: false)
        }
    }

    private func ensureAttached() {
        guard let root = attachedRootViewController else { return }
        if owningViewController == nil { owningViewController = topMost(from: root) }
        if let vc = owningViewController, objc_getAssociatedObject(vc, &Self.kInstalledKey) == nil {
            swizzleIfNeeded(on: vc)
        }
    }

    private func swizzleIfNeeded(on vc: UIViewController) {
        if objc_getAssociatedObject(vc, &Self.kInstalledKey) != nil { return }
        guard let origClass: AnyClass = object_getClass(vc) else { return }
        let subclassName = "\(NSStringFromClass(origClass))_StatusBarProxy"
        let subclass: AnyClass
        if let existing = NSClassFromString(subclassName) {
            subclass = existing
        } else {
            guard let sub = objc_allocateClassPair(origClass, subclassName, 0) else { return }

            let prefersHiddenBlock: @convention(block) (AnyObject) -> Bool = { obj in
                (objc_getAssociatedObject(obj, &StatusBarManager.kHiddenKey) as? NSNumber)?.boolValue ?? false
            }
            class_addMethod(sub,
                            #selector(getter: UIViewController.prefersStatusBarHidden),
                            imp_implementationWithBlock(prefersHiddenBlock),
                            "B@:")

            let styleBlock: @convention(block) (AnyObject) -> Int = { obj in
                (objc_getAssociatedObject(obj, &StatusBarManager.kStyleKey) as? NSNumber)?.intValue
                    ?? UIStatusBarStyle.default.rawValue
            }
            class_addMethod(sub,
                            #selector(getter: UIViewController.preferredStatusBarStyle),
                            imp_implementationWithBlock(styleBlock),
                            "q@:")

            let childNilBlock: @convention(block) (AnyObject) -> AnyObject? = { _ in nil }
            class_addMethod(sub,
                            #selector(getter: UIViewController.childForStatusBarHidden),
                            imp_implementationWithBlock(childNilBlock),
                            "@@:")
            class_addMethod(sub,
                            #selector(getter: UIViewController.childForStatusBarStyle),
                            imp_implementationWithBlock(childNilBlock),
                            "@@:")

            objc_registerClassPair(sub)
            subclass = sub
        }

        object_setClass(vc, subclass)
        objc_setAssociatedObject(vc, &Self.kInstalledKey, true, .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
        objc_setAssociatedObject(vc, &Self.kHiddenKey, NSNumber(value: hidden), .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
        objc_setAssociatedObject(vc, &Self.kStyleKey, NSNumber(value: style.rawValue), .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
        vc.setNeedsStatusBarAppearanceUpdate()
    }

    private func topMost(from root: UIViewController) -> UIViewController {
        var viewController = root
        while let p = viewController.presentedViewController {
            viewController = p
        }
        return viewController
    }
}

public extension StatusBarManager {
    static func install(on root: UIViewController,
                        hidden: Bool = false,
                        style: UIStatusBarStyle = .default,
                        animated: Bool = false)
    {
        DispatchQueue.main.async {
            StatusBarManager.shared.attach(to: root)
            StatusBarManager.shared.setHidden(hidden, style: style, animated: animated)
        }
    }
}

public extension View {
    func statusBarHidden(_ hidden: Bool,
                         style: UIStatusBarStyle? = nil,
                         animated: Bool = true) -> some View
    {
        onAppear {
            StatusBarManager.shared.setHidden(hidden, style: style, animated: animated)
        }
        .onChange(of: hidden) { v in
            StatusBarManager.shared.setHidden(v, style: style, animated: animated)
        }
    }
}
