package javax.swing.plaf.metal;

import java.awt.Color;
import java.awt.Component;
import java.awt.Font;
import java.awt.Graphics;
import java.awt.Insets;
import java.util.Arrays;
import javax.swing.AbstractButton;
import javax.swing.ButtonModel;
import javax.swing.Icon;
import javax.swing.UIDefaults;
import javax.swing.plaf.ColorUIResource;
import javax.swing.plaf.FontUIResource;
import javax.swing.plaf.IconUIResource;
import sun.swing.PrintColorUIResource;
import sun.swing.SwingLazyValue;
import sun.swing.SwingUtilities2;

public class CopperChlorideTheme extends OceanTheme {

    private ColorUIResource shade(double factor, ColorUIResource source) {
        if (factor == 1.0) {
            return source;
        }
        return new ColorUIResource(
                Math.min(255, (int) (source.getRed() * factor)),
                Math.min(255, (int) (source.getGreen() * factor)),
                Math.min(255, (int) (source.getBlue() * factor)));
    }

    private final ColorUIResource BLACK = new ColorUIResource(0, 0, 0);
    private final FontUIResource FONT = new FontUIResource(Font.SANS_SERIF, Font.PLAIN, 8);
    private static final ColorUIResource SECONDARY1 =
            new ColorUIResource(0x7A8A99);
    private static final ColorUIResource SECONDARY2 =
            new ColorUIResource(0xB8CFE5);
    private static final ColorUIResource SECONDARY3 =
            new ColorUIResource(0xEEEEEE);
    private static final ColorUIResource INACTIVE_CONTROL_TEXT_COLOR =
            new ColorUIResource(0x999999);
    private static final ColorUIResource OCEAN_BLACK =
            new PrintColorUIResource(0x333333, Color.BLACK);
    private static final ColorUIResource OCEAN_DROP =
            new ColorUIResource(0xD2E9FF);

    @Override
    public String getName() {
        return "CopperChloride";
    }

    private Object getIconResource(String iconID) {
        return SwingUtilities2.makeIcon(getClass(), OceanTheme.class, iconID);
    }

    private Icon getHastenedIcon(String iconID, UIDefaults table) {
        Object res = getIconResource(iconID);
        return (Icon) ((UIDefaults.LazyValue) res).createValue(table);
    }

    private static class COIcon extends IconUIResource {

        private Icon rtl;

        public COIcon(Icon ltr, Icon rtl) {
            super(ltr);
            this.rtl = rtl;
        }

        public void paintIcon(Component c, Graphics g, int x, int y) {
            if (c.getComponentOrientation().isLeftToRight()) {
                super.paintIcon(c, g, x, y);
            } else {
                rtl.paintIcon(c, g, x, y);
            }
        }

    }

    private static class IFIcon extends IconUIResource {

        private Icon pressed;

        public IFIcon(Icon normal, Icon pressed) {
            super(normal);
            this.pressed = pressed;
        }

        public void paintIcon(Component c, Graphics g, int x, int y) {
            ButtonModel model = ((AbstractButton) c).getModel();
            if (model.isPressed() && model.isArmed()) {
                pressed.paintIcon(c, g, x, y);
            } else {
                super.paintIcon(c, g, x, y);
            }
        }

    }

    @Override
    public void addCustomEntriesToTable(UIDefaults table) {
        Object focusBorder = new SwingLazyValue(
                "javax.swing.plaf.BorderUIResource$LineBorderUIResource",
                new Object[]{getPrimary1()});

        ColorUIResource bgColor = new ColorUIResource(0xD5DAD5);

        java.util.List buttonGradient = Arrays.asList(
                new Object[]{
                    new Float(0.25f),
                    new Float(0f),
                    shade(1.0, shade(0.85, bgColor)),
                    shade(1.1, shade(0.85, bgColor)),
                    shade(0.8, shade(0.85, bgColor))
                });

        // Other possible properties that aren't defined:
        //
        // Used when generating the disabled Icons, provides the region to
        // constrain grays to.
        // Button.disabledGrayRange -> Object[] of Integers giving min/max
        // InternalFrame.inactiveTitleGradient -> Gradient when the
        //   internal frame is inactive.
        Color cccccc = new ColorUIResource(0xCCCCCC);
        Color dadada = new ColorUIResource(0xDADADA);
        Color c8ddf2 = new ColorUIResource(0xC8DDF2);
        Color BFCCB5 = new ColorUIResource(0xBFCCB5);
        Object directoryIcon = getIconResource("icons/ocean/directory.gif");
        Object fileIcon = getIconResource("icons/ocean/file.gif");
        java.util.List sliderGradient = Arrays.asList(new Object[]{
                    new Float(0f), new Float(1f),
                    getWhite(), BFCCB5, new ColorUIResource(SECONDARY2)});

        Object[] defaults = new Object[]{
            "Button.gradient", buttonGradient,
            "Button.rollover", Boolean.TRUE,
            "Button.toolBarBorderBackground", INACTIVE_CONTROL_TEXT_COLOR,
            "Button.disabledToolBarBorderBackground", cccccc,
            "Button.rolloverIconType", "ocean",
            "CheckBox.rollover", Boolean.TRUE,
            "CheckBox.gradient", buttonGradient,
            "CheckBoxMenuItem.gradient", buttonGradient,
            // home2
            "FileChooser.homeFolderIcon",
            getIconResource("icons/ocean/homeFolder.gif"),
            // directory2
            "FileChooser.newFolderIcon",
            getIconResource("icons/ocean/newFolder.gif"),
            // updir2
            "FileChooser.upFolderIcon",
            getIconResource("icons/ocean/upFolder.gif"),
            // computer2
            "FileView.computerIcon",
            getIconResource("icons/ocean/computer.gif"),
            "FileView.directoryIcon", directoryIcon,
            // disk2
            "FileView.hardDriveIcon",
            getIconResource("icons/ocean/hardDrive.gif"),
            "FileView.fileIcon", fileIcon,
            // floppy2
            "FileView.floppyDriveIcon",
            getIconResource("icons/ocean/floppy.gif"),
            "Label.disabledForeground", getInactiveControlTextColor(),
            "Menu.opaque", Boolean.FALSE,
            "MenuBar.gradient", Arrays.asList(new Object[]{
                new Float(1f), new Float(0f),
                getWhite(), dadada,
                new ColorUIResource(dadada)}),
            "MenuBar.borderColor", cccccc,
            "InternalFrame.activeTitleGradient", buttonGradient,
            // close2
            "InternalFrame.closeIcon",
            new UIDefaults.LazyValue() {

                public Object createValue(UIDefaults table) {
                    return new IFIcon(getHastenedIcon("icons/ocean/close.gif", table),
                            getHastenedIcon("icons/ocean/close-pressed.gif", table));
                }

            },
            // minimize
            "InternalFrame.iconifyIcon",
            new UIDefaults.LazyValue() {

                public Object createValue(UIDefaults table) {
                    return new IFIcon(getHastenedIcon("icons/ocean/iconify.gif", table),
                            getHastenedIcon("icons/ocean/iconify-pressed.gif", table));
                }

            },
            // restore
            "InternalFrame.minimizeIcon",
            new UIDefaults.LazyValue() {

                public Object createValue(UIDefaults table) {
                    return new IFIcon(getHastenedIcon("icons/ocean/minimize.gif", table),
                            getHastenedIcon("icons/ocean/minimize-pressed.gif", table));
                }

            },
            // menubutton3
            "InternalFrame.icon",
            getIconResource("icons/ocean/menu.gif"),
            // maximize2
            "InternalFrame.maximizeIcon",
            new UIDefaults.LazyValue() {

                public Object createValue(UIDefaults table) {
                    return new IFIcon(getHastenedIcon("icons/ocean/maximize.gif", table),
                            getHastenedIcon("icons/ocean/maximize-pressed.gif", table));
                }

            },
            // paletteclose
            "InternalFrame.paletteCloseIcon",
            new UIDefaults.LazyValue() {

                public Object createValue(UIDefaults table) {
                    return new IFIcon(getHastenedIcon("icons/ocean/paletteClose.gif", table),
                            getHastenedIcon("icons/ocean/paletteClose-pressed.gif", table));
                }

            },
            "List.focusCellHighlightBorder", focusBorder,
            "MenuBarUI", "javax.swing.plaf.metal.MetalMenuBarUI",
            "OptionPane.errorIcon",
            getIconResource("icons/ocean/error.png"),
            "OptionPane.informationIcon",
            getIconResource("icons/ocean/info.png"),
            "OptionPane.questionIcon",
            getIconResource("icons/ocean/question.png"),
            "OptionPane.warningIcon",
            getIconResource("icons/ocean/warning.png"),
            "RadioButton.gradient", buttonGradient,
            "RadioButton.rollover", Boolean.TRUE,
            "RadioButtonMenuItem.gradient", buttonGradient,
            "ScrollBar.gradient", buttonGradient,
            "Slider.altTrackColor", new ColorUIResource(0xD2E2EF),
            "Slider.gradient", sliderGradient,
            "Slider.focusGradient", sliderGradient,
            "SplitPane.oneTouchButtonsOpaque", Boolean.FALSE,
            "SplitPane.dividerFocusColor", c8ddf2,
            "TabbedPane.borderHightlightColor", getPrimary1(),
            "TabbedPane.contentAreaColor", c8ddf2,
            "TabbedPane.contentBorderInsets", new Insets(4, 2, 3, 3),
            "TabbedPane.selected", c8ddf2,
            "TabbedPane.tabAreaBackground", dadada,
            "TabbedPane.tabAreaInsets", new Insets(2, 2, 0, 6),
            "TabbedPane.unselectedBackground", SECONDARY3,
            "Table.focusCellHighlightBorder", focusBorder,
            "Table.gridColor", SECONDARY1,
            "TableHeader.focusCellBackground", c8ddf2,
            "ToggleButton.gradient", buttonGradient,
            "ToolBar.borderColor", cccccc,
            "ToolBar.isRollover", Boolean.TRUE,
            "Tree.closedIcon", directoryIcon,
            "Tree.collapsedIcon",
            new UIDefaults.LazyValue() {

                public Object createValue(UIDefaults table) {
                    return new COIcon(getHastenedIcon("icons/ocean/collapsed.gif", table),
                            getHastenedIcon("icons/ocean/collapsed-rtl.gif", table));
                }

            },
            "Tree.expandedIcon",
            getIconResource("icons/ocean/expanded.gif"),
            "Tree.leafIcon", fileIcon,
            "Tree.openIcon", directoryIcon,
            "Tree.selectionBorderColor", getPrimary1(),
            "Tree.dropLineColor", getPrimary1(),
            "Table.dropLineColor", getPrimary1(),
            "Table.dropLineShortColor", OCEAN_BLACK,
            "Table.dropCellBackground", OCEAN_DROP,
            "Tree.dropCellBackground", OCEAN_DROP,
            "List.dropCellBackground", OCEAN_DROP,
            "List.dropLineColor", getPrimary1()
        };
        table.putDefaults(defaults);
    }

    @Override
    public ColorUIResource getAcceleratorForeground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getAcceleratorSelectedForeground() {
        return BLACK;
    }

    @Override
    protected ColorUIResource getBlack() {
        return BLACK;
    }

    @Override
    public ColorUIResource getControl() {
        return BLACK;
    }

    @Override
    public ColorUIResource getControlDarkShadow() {
        return BLACK;
    }

    @Override
    public ColorUIResource getControlDisabled() {
        return BLACK;
    }

    @Override
    public ColorUIResource getControlHighlight() {
        return BLACK;
    }

    @Override
    public ColorUIResource getControlInfo() {
        return BLACK;
    }

    @Override
    public ColorUIResource getControlShadow() {
        return BLACK;
    }

    @Override
    public ColorUIResource getControlTextColor() {
        return BLACK;
    }

    @Override
    public ColorUIResource getDesktopColor() {
        return BLACK;
    }

    @Override
    public ColorUIResource getFocusColor() {
        return BLACK;
    }

    @Override
    public ColorUIResource getHighlightedTextColor() {
        return BLACK;
    }

    @Override
    public ColorUIResource getInactiveControlTextColor() {
        return BLACK;
    }

    @Override
    public ColorUIResource getInactiveSystemTextColor() {
        return BLACK;
    }

    @Override
    public ColorUIResource getMenuBackground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getMenuDisabledForeground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getMenuForeground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getMenuSelectedBackground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getMenuSelectedForeground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getPrimaryControl() {
        return BLACK;
    }

    @Override
    public ColorUIResource getPrimaryControlDarkShadow() {
        return BLACK;
    }

    @Override
    public ColorUIResource getPrimaryControlHighlight() {
        return BLACK;
    }

    @Override
    public ColorUIResource getPrimaryControlInfo() {
        return BLACK;
    }

    @Override
    public ColorUIResource getPrimaryControlShadow() {
        return BLACK;
    }

    @Override
    public ColorUIResource getSeparatorBackground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getSeparatorForeground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getSystemTextColor() {
        return BLACK;
    }

    @Override
    public ColorUIResource getTextHighlightColor() {
        return BLACK;
    }

    @Override
    public ColorUIResource getUserTextColor() {
        return BLACK;
    }

    @Override
    protected ColorUIResource getWhite() {
        return BLACK;
    }

    @Override
    public ColorUIResource getWindowBackground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getWindowTitleBackground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getWindowTitleForeground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getWindowTitleInactiveBackground() {
        return BLACK;
    }

    @Override
    public ColorUIResource getWindowTitleInactiveForeground() {
        return BLACK;
    }

    @Override
    void install() {
    }

    @Override
    boolean isSystemTheme() {
        return true;
    }

    @Override
    protected ColorUIResource getPrimary1() {
        return BLACK;
    }

    @Override
    protected ColorUIResource getPrimary2() {
        return BLACK;
    }

    @Override
    protected ColorUIResource getPrimary3() {
        return BLACK;
    }

    @Override
    protected ColorUIResource getSecondary1() {
        return BLACK;
    }

    @Override
    protected ColorUIResource getSecondary2() {
        return BLACK;
    }

    @Override
    protected ColorUIResource getSecondary3() {
        return BLACK;
    }

    @Override
    public FontUIResource getControlTextFont() {
        return FONT;
    }

    @Override
    public FontUIResource getSystemTextFont() {
        return FONT;
    }

    @Override
    public FontUIResource getUserTextFont() {
        return FONT;
    }

    @Override
    public FontUIResource getMenuTextFont() {
        return FONT;
    }

    @Override
    public FontUIResource getWindowTitleFont() {
        return FONT;
    }

    @Override
    public FontUIResource getSubTextFont() {
        return FONT;
    }

}
