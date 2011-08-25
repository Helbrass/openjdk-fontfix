package copperchloride;

import javax.swing.plaf.metal.CopperChlorideTheme;
import javax.swing.plaf.metal.MetalLookAndFeel;
import swingset2.SwingSet2;

public class Copperchloride {

    public static void main(String[] args) {
        MetalLookAndFeel.setCurrentTheme(new CopperChlorideTheme());
        SwingSet2.main(args);
    }

}
