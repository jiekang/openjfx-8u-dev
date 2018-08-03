/*
 * Copyright (c) 2017, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package com.sun.webkit;

import com.sun.javafx.webkit.prism.PrismInvokerShim;
import com.sun.webkit.WebPage;
import com.sun.webkit.graphics.WCRectangle;

public class WebPageShim {

    public static int getFramesCount(WebPage page) {
        return page.test_getFramesCount();
    }

    public static void renderContent(WebPage page, int x, int y, int w, int h) {
        page.setBounds(x, y, w, h);
        //  WebPage.updateContent will render WebPage into RenderQueue.
        page.updateContent(new WCRectangle(x, y, w, h));
        //  WebPage.paint will render RenderQueue into WCGraphicsContext in RenderThread.
        PrismInvokerShim.runOnRenderThread(() -> {
            // NullPointerException is expected because we are passing
            // front buffer WCGraphicsContext as null.
            try {
                page.paint(null, x, y, w, h);
            } catch (NullPointerException e) {}
        });
    }
}
