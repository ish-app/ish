//
//  HostingController.swift
//  watch Extension
//
//  Created by Alex Gustafsson on 2020-07-26.
//

import WatchKit
import Foundation
import SwiftUI
import WatchConnectivity

class HostingController: WKHostingController<ContentView>, WCSessionDelegate {
    var wcSession : WCSession!

    func session(_ session: WCSession, activationDidCompleteWith activationState: WCSessionActivationState, error: Error?) {
        print("activationDidComplete", activationState)
    }

    func session(_ session: WCSession, didReceiveMessage message: [String : Any]) {
        let text = message["message"] as! String
        print("Received message \(text)")
    }
    
    override func willActivate() {
        super.willActivate()

        if WCSession.isSupported() {
            print("WCSession is supported. Activating watch communication session")
            self.wcSession = WCSession.default
            self.wcSession.delegate = self
            self.wcSession.activate()
            print("Watch communication session active")
        } else {
            print("Watch communication is not supported")
        }
    }
    
    func sessionReachabilityDidChange(_ session: WCSession) {
        print("Watch companion reachability changed to \(session.isReachable)")
    }
    
    override var body: ContentView {
        return ContentView()
    }
}
