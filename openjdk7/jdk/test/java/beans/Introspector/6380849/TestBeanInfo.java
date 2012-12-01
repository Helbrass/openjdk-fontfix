/**
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
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

/*
 * @test
 * @bug 6380849
 * @summary Tests BeanInfo finder
 * @author Sergey Malenkov
 */

import beans.FirstBean;
import beans.FirstBeanBeanInfo;
import beans.SecondBean;
import beans.ThirdBean;

import infos.SecondBeanBeanInfo;
import infos.ThirdBeanBeanInfo;

import java.beans.BeanInfo;
import java.beans.Introspector;
import java.lang.ref.Reference;
import java.lang.reflect.Field;

import sun.awt.SunToolkit;

public class TestBeanInfo implements Runnable {

    private static final String[] SEARCH_PATH = { "infos" }; // NON-NLS: package name

    public static void main(String[] args) throws InterruptedException {
        TestBeanInfo test = new TestBeanInfo();
        test.run();
        // the following tests fails on previous build
        ThreadGroup group = new ThreadGroup("$$$"); // NON-NLS: unique thread name
        Thread thread = new Thread(group, test);
        thread.start();
        thread.join();
    }

    private static void test(Class<?> type, Class<? extends BeanInfo> expected) {
        BeanInfo actual;
        try {
            actual = Introspector.getBeanInfo(type);
            type = actual.getClass();
            Field field = type.getDeclaredField("targetBeanInfoRef"); // NON-NLS: field name
            field.setAccessible(true);
            Reference ref = (Reference) field.get(actual);
            actual = (BeanInfo) ref.get();
        }
        catch (Exception exception) {
            throw new Error("unexpected error", exception);
        }
        if ((actual == null) && (expected != null)) {
            throw new Error("expected info is not found");
        }
        if ((actual != null) && !actual.getClass().equals(expected)) {
            throw new Error("found unexpected info");
        }
    }

    private boolean passed;

    public void run() {
        if (this.passed) {
            SunToolkit.createNewAppContext();
        }
        Introspector.flushCaches();

        test(FirstBean.class, FirstBeanBeanInfo.class);
        test(SecondBean.class, null);
        test(ThirdBean.class, null);
        test(ThirdBeanBeanInfo.class, ThirdBeanBeanInfo.class);

        Introspector.setBeanInfoSearchPath(SEARCH_PATH);
        Introspector.flushCaches();

        test(FirstBean.class, FirstBeanBeanInfo.class);
        test(SecondBean.class, SecondBeanBeanInfo.class);
        test(ThirdBean.class, null);
        test(ThirdBeanBeanInfo.class, ThirdBeanBeanInfo.class);

        this.passed = true;
    }
}
