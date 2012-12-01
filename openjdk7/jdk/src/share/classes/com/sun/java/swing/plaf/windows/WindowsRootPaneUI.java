/*
 * Copyright (c) 2000, 2006, Oracle and/or its affiliates. All rights reserved.
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

package com.sun.java.swing.plaf.windows;

import java.awt.Component;
import java.awt.Container;
import java.awt.Event;
import java.awt.KeyEventPostProcessor;
import java.awt.Window;

import java.awt.event.ActionEvent;
import java.awt.event.KeyEvent;

import javax.swing.AbstractAction;
import javax.swing.ActionMap;
import javax.swing.InputMap;
import javax.swing.KeyStroke;
import javax.swing.JComponent;
import javax.swing.JLabel;
import javax.swing.JRootPane;
import javax.swing.SwingUtilities;
import javax.swing.UIManager;
import javax.swing.AbstractButton;
import javax.swing.JFrame;
import javax.swing.JMenu;
import javax.swing.JMenuBar;
import javax.swing.MenuElement;
import javax.swing.MenuSelectionManager;

import javax.swing.plaf.ActionMapUIResource;
import javax.swing.plaf.ComponentUI;
import javax.swing.plaf.InputMapUIResource;

import javax.swing.plaf.basic.BasicRootPaneUI;
import javax.swing.plaf.basic.ComboPopup;

/**
 * Windows implementation of RootPaneUI, there is one shared between all
 * JRootPane instances.
 *
 * @author Mark Davidson
 * @since 1.4
 */
public class WindowsRootPaneUI extends BasicRootPaneUI {

    private final static WindowsRootPaneUI windowsRootPaneUI = new WindowsRootPaneUI();
    static final AltProcessor altProcessor = new AltProcessor();

    public static ComponentUI createUI(JComponent c) {
        return windowsRootPaneUI;
    }

    static class AltProcessor implements KeyEventPostProcessor {
        static boolean altKeyPressed = false;
        static boolean menuCanceledOnPress = false;
        static JRootPane root = null;
        static Window winAncestor = null;

        void altPressed(KeyEvent ev) {
            MenuSelectionManager msm =
                MenuSelectionManager.defaultManager();
            MenuElement[] path = msm.getSelectedPath();
            if (path.length > 0 && ! (path[0] instanceof ComboPopup)) {
                msm.clearSelectedPath();
                menuCanceledOnPress = true;
                ev.consume();
            } else if(path.length > 0) { // We are in ComboBox
                menuCanceledOnPress = false;
                WindowsLookAndFeel.setMnemonicHidden(false);
                WindowsGraphicsUtils.repaintMnemonicsInWindow(winAncestor);
                ev.consume();
            } else {
                menuCanceledOnPress = false;
                WindowsLookAndFeel.setMnemonicHidden(false);
                WindowsGraphicsUtils.repaintMnemonicsInWindow(winAncestor);
                JMenuBar mbar = root != null ? root.getJMenuBar() : null;
                if(mbar == null && winAncestor instanceof JFrame) {
                    mbar = ((JFrame)winAncestor).getJMenuBar();
                }
                JMenu menu = mbar != null ? mbar.getMenu(0) : null;
                if(menu != null) {
                    ev.consume();
                }
            }
        }

        void altReleased(KeyEvent ev) {
            if (menuCanceledOnPress) {
                WindowsLookAndFeel.setMnemonicHidden(true);
                WindowsGraphicsUtils.repaintMnemonicsInWindow(winAncestor);
                return;
            }

            MenuSelectionManager msm =
                MenuSelectionManager.defaultManager();
            if (msm.getSelectedPath().length == 0) {
                // if no menu is active, we try activating the menubar

                JMenuBar mbar = root != null ? root.getJMenuBar() : null;
                if(mbar == null && winAncestor instanceof JFrame) {
                    mbar = ((JFrame)winAncestor).getJMenuBar();
                }
                JMenu menu = mbar != null ? mbar.getMenu(0) : null;

                if (menu != null) {
                    MenuElement[] path = new MenuElement[2];
                    path[0] = mbar;
                    path[1] = menu;
                    msm.setSelectedPath(path);
                } else if(!WindowsLookAndFeel.isMnemonicHidden()) {
                    WindowsLookAndFeel.setMnemonicHidden(true);
                    WindowsGraphicsUtils.repaintMnemonicsInWindow(winAncestor);
                }
            } else {
                if((msm.getSelectedPath())[0] instanceof ComboPopup) {
                    WindowsLookAndFeel.setMnemonicHidden(true);
                    WindowsGraphicsUtils.repaintMnemonicsInWindow(winAncestor);
                }
            }

        }

        public boolean postProcessKeyEvent(KeyEvent ev) {
            if(ev.isConsumed()) {
                // do not manage consumed event
                return false;
            }
            if (ev.getKeyCode() == KeyEvent.VK_ALT) {
                root = SwingUtilities.getRootPane(ev.getComponent());
                winAncestor = (root == null ? null :
                        SwingUtilities.getWindowAncestor(root));

                if (ev.getID() == KeyEvent.KEY_PRESSED) {
                    if (!altKeyPressed) {
                        altPressed(ev);
                    }
                    altKeyPressed = true;
                    return true;
                } else if (ev.getID() == KeyEvent.KEY_RELEASED) {
                    if (altKeyPressed) {
                        altReleased(ev);
                    } else {
                        MenuSelectionManager msm =
                            MenuSelectionManager.defaultManager();
                        MenuElement[] path = msm.getSelectedPath();
                        if (path.length <= 0) {
                            WindowsLookAndFeel.setMnemonicHidden(true);
                            WindowsGraphicsUtils.repaintMnemonicsInWindow(winAncestor);
                        }
                    }
                    altKeyPressed = false;
                }
                root = null;
                winAncestor = null;
            } else {
                altKeyPressed = false;
            }
            return false;
        }
    }
}
