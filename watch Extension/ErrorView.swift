//
//  ErrorView.swift
//  watch Extension
//
//  Created by Alex Gustafsson on 2020-07-26.
//

import SwiftUI

struct ErrorView: View {
    var message: String
    var error: Error?
    
    var body: some View {
        VStack {
            Text(self.message)
            if self.error != nil {
                Text(self.error!.localizedDescription)
            }
        }
    }
}

struct ErrorView_Previews: PreviewProvider {
    static var previews: some View {
        ErrorView(message: "Example error", error: NSError(domain: "Example error", code: 69))
    }
}
